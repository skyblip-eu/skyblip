#if defined(__ZEPHYR__)

#include "hardware/platform/zephyr/link.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>

#include "core/util/fifo.h"

namespace skyblip::platform::zephyr {

using skyblip::messages::Endpoint;
using skyblip::messages::RxFrame;

namespace {

// skyBlip companion service (random 128-bit base, align with the app spec).
//   service   6e40-0001-...  NMEA notify   6e40-0002-...  config r/w/notify 6e40-0003-...
//   flight log r/w/notify 6e40-0004-...
#define SKB_UUID(v) BT_UUID_128_ENCODE(0x6e400000 | (v), 0xb5a3, 0xf393, 0xe0a9, 0xe50e24dcca9e)
static struct bt_uuid_128 svc_uuid = BT_UUID_INIT_128(SKB_UUID(0x0001));
static struct bt_uuid_128 nmea_uuid = BT_UUID_INIT_128(SKB_UUID(0x0002));
static struct bt_uuid_128 cfg_uuid = BT_UUID_INIT_128(SKB_UUID(0x0003));
// The log offload is thousands of small round trips. On its own characteristic
// it cannot starve the one the pilot's prompts travel on, and an app that does
// not want logs simply never subscribes.
static struct bt_uuid_128 log_uuid = BT_UUID_INIT_128(SKB_UUID(0x0004));

Fifo<RxFrame, 8> g_rx;
struct k_spinlock g_lock;
struct bt_conn* g_conn = nullptr;
bool g_nmea_subscribed = false;
bool g_cfg_subscribed = false;
bool g_log_subscribed = false;

void push_rx(struct bt_conn* conn, Endpoint endpoint, const void* buf, uint16_t len) {
    RxFrame f{};
    f.session_id = static_cast<uint16_t>(reinterpret_cast<uintptr_t>(conn));
    f.endpoint = endpoint;
    f.len = len > f.data.size() ? static_cast<uint16_t>(f.data.size()) : len;
    const uint8_t* p = static_cast<const uint8_t*>(buf);
    for (uint16_t i = 0; i < f.len; i++) f.data[i] = p[i];
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    g_rx.push(f);
    k_spin_unlock(&g_lock, key);
}

ssize_t on_cfg_write(struct bt_conn* conn, const struct bt_gatt_attr*, const void* buf,
                     uint16_t len, uint16_t /*offset*/, uint8_t /*flags*/) {
    push_rx(conn, Endpoint::Config, buf, len);
    return len;
}

ssize_t on_log_write(struct bt_conn* conn, const struct bt_gatt_attr*, const void* buf,
                     uint16_t len, uint16_t /*offset*/, uint8_t /*flags*/) {
    push_rx(conn, Endpoint::Log, buf, len);
    return len;
}

void nmea_ccc(const struct bt_gatt_attr*, uint16_t value) {
    g_nmea_subscribed = (value == BT_GATT_CCC_NOTIFY);
}
void cfg_ccc(const struct bt_gatt_attr*, uint16_t value) {
    g_cfg_subscribed = (value == BT_GATT_CCC_NOTIFY);
}
void log_ccc(const struct bt_gatt_attr*, uint16_t value) {
    g_log_subscribed = (value == BT_GATT_CCC_NOTIFY);
}

// attrs[]: [0]=service [1]=nmea decl [2]=nmea value [3]=nmea ccc
//          [4]=cfg decl [5]=cfg value [6]=cfg ccc
//          [7]=log decl [8]=log value [9]=log ccc
BT_GATT_SERVICE_DEFINE(skb_svc, BT_GATT_PRIMARY_SERVICE(&svc_uuid),
                       BT_GATT_CHARACTERISTIC(&nmea_uuid.uuid, BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_NONE, nullptr, nullptr, nullptr),
                       BT_GATT_CCC(nmea_ccc, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
                       BT_GATT_CHARACTERISTIC(&cfg_uuid.uuid,
                                              BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP |
                                                  BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_WRITE, nullptr, on_cfg_write, nullptr),
                       BT_GATT_CCC(cfg_ccc, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
                       BT_GATT_CHARACTERISTIC(&log_uuid.uuid,
                                              BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP |
                                                  BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_WRITE, nullptr, on_log_write, nullptr),
                       BT_GATT_CCC(log_ccc, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));

void connected(struct bt_conn* conn, uint8_t err) {
    if (!err) g_conn = bt_conn_ref(conn);
}
void disconnected(struct bt_conn* conn, uint8_t /*reason*/) {
    if (g_conn == conn) {
        bt_conn_unref(g_conn);
        g_conn = nullptr;
    }
}
BT_CONN_CB_DEFINE(conn_cbs) = {.connected = connected, .disconnected = disconnected};

const struct bt_data adv[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
    BT_DATA(BT_DATA_NAME_COMPLETE, "skyBlip", 7),
};

}  // namespace

Status Link::begin() {
    if (bt_enable(nullptr) != 0) return Status::Down;
    if (bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, adv, ARRAY_SIZE(adv), nullptr, 0) != 0)
        return Status::Down;
    return Status::Ok;
}

Status Link::send(Endpoint ep, ConstByteSpan bytes) {
    if (!g_conn) return Status::Down;
    // attr index of the value handle for each characteristic (see table above).
    int index = 5;
    bool subbed = g_cfg_subscribed;
    if (ep == Endpoint::Nmea) {
        index = 2;
        subbed = g_nmea_subscribed;
    } else if (ep == Endpoint::Log) {
        index = 8;
        subbed = g_log_subscribed;
    }
    const struct bt_gatt_attr* value_attr = &skb_svc.attrs[index];
    if (!subbed) return Status::WouldBlock;
    int rc = bt_gatt_notify(g_conn, value_attr, bytes.data(), bytes.size());
    if (rc == -ENOMEM || rc == -EAGAIN) return Status::WouldBlock;
    return rc == 0 ? Status::Ok : Status::Invalid;
}

bool Link::pop_rx(RxFrame& out) {
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    Result<RxFrame> r = g_rx.pop();
    k_spin_unlock(&g_lock, key);
    if (!r.ok()) return false;
    out = r.value();
    return true;
}

Link& link() {
    static Link instance;
    return instance;
}

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
