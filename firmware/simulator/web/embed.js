import createModule from './skyblip_simulator.js';

export const PAGES = ['radar', '6-pack', 'status', 'signal', 'radio log', 'settings'];
export const ALARM = ['none', 'info', 'important', 'urgent'];
export const SHUTDOWN = ['running', 'parking', 'await release', 'off'];
export const REFRESH = ['idle', 'partial refresh', 'full refresh'];
export const DWELL = ['uplink RX · 869.525', 'retune O->M', 'slot 0 · 868.200',
                      'hop', 'slot 1 · 868.400', 'retune M->O'];

const SLOT_EDGE_STEP_MS = 5;
const INK = [20, 20, 20];
const PAPER = [201, 201, 196];

export async function load() {
  const module = await createModule();
  const call = (name, ret, args) => module.cwrap(name, ret, args);
  const n = [];
  const num = ['number'];

  const sim = {
    setup: call('simulator_setup', null, n),
    step: call('simulator_step', null, num),
    loadScenario: call('simulator_load_scenario', 'number', ['string', 'number']),
    mode: call('simulator_mode', 'number', n),
    failures: call('simulator_failures', 'number', n),

    button: call('simulator_button', null, n),
    holdButton: call('simulator_button_down', null, num),
    holdPad: call('simulator_pad_down', null, num),
    backlight: call('simulator_backlight', null, num),
    power: call('simulator_power', null, num),

    setRange: call('simulator_set_range', null, num),
    setFix: call('simulator_set_fix', null, num),
    setPps: call('simulator_set_pps', null, num),
    setSats: call('simulator_set_sats', null, num),
    setAlt: call('simulator_set_alt', null, num),
    setSpeed: call('simulator_set_speed', null, num),
    setTrack: call('simulator_set_track', null, num),
    setClimb: call('simulator_set_climb', null, num),
    setQnh: call('simulator_set_qnh', null, num),
    setAirmass: call('simulator_set_airmass', null, num),
    setBatteryMv: call('simulator_set_battery_mv', null, num),
    setExternalPower: call('simulator_set_external_power', null, num),

    addAircraft: call('simulator_add_aircraft', null, Array(6).fill('number')),
    addAircraftAt: call('simulator_add_aircraft_at', null, Array(8).fill('number')),
    addThreat: call('simulator_add_threat', null, num),
    clearTraffic: call('simulator_clear_traffic', null, n),

    fb: call('simulator_fb', 'number', n),
    W: call('simulator_fb_w', 'number', n)(),
    H: call('simulator_fb_h', 'number', n)(),
    STRIDE: call('simulator_fb_stride', 'number', n)(),
    refreshing: call('simulator_panel_refreshing', 'number', n),
    refreshIsFull: call('simulator_panel_refresh_is_full', 'number', n),
    presents: call('simulator_present_count', 'number', n),
    blOn: call('simulator_backlight_on', 'number', n),
    powered: call('simulator_powered', 'number', n),
    page: call('simulator_page', 'number', n),
    shutdownPhase: call('simulator_shutdown_phase', 'number', n),

    fixValid: call('simulator_fix_valid', 'number', n),
    sats: call('simulator_sats', 'number', n),
    lat: call('simulator_lat_1e7', 'number', n),
    lon: call('simulator_lon_1e7', 'number', n),
    alt: call('simulator_alt_m', 'number', n),
    speedQ: call('simulator_speed_q', 'number', n),
    trackC9: call('simulator_track_c9', 'number', n),
    climbE8: call('simulator_climb_e8', 'number', n),
    pressurePa: call('simulator_pressure_pa', 'number', n),
    batteryMv: call('simulator_battery_mv', 'number', n),
    batteryPercent: call('simulator_battery_percent', 'number', n),
    batteryCharging: call('simulator_battery_charging', 'number', n),

    traffic: call('simulator_traffic_count', 'number', n),
    alarm: call('simulator_alarm_level', 'number', n),
    rxOk: call('simulator_rx_ok', 'number', n),
    rxBad: call('simulator_rx_bad', 'number', n),
    txOk: call('simulator_tx_ok', 'number', n),
    txBusy: call('simulator_tx_busy', 'number', n),
    slotState: call('simulator_slot_state', 'number', n),
    dwellFreq: call('simulator_dwell_freq', 'number', n),
    airCount: call('simulator_air_count', 'number', n),
    airPhase: call('simulator_air_phase_ms', 'number', num),
    airEvent: call('simulator_air_event', 'number', num),
    airFreqKhz: call('simulator_air_freq_khz', 'number', num),
    airLine: call('simulator_air_line', 'string', num),
  };

  let simMs = 0;
  let image = null;

  sim.elapsedMs = () => simMs;

  sim.advance = (targetMs) => {
    while (simMs + SLOT_EDGE_STEP_MS <= targetMs) {
      simMs += SLOT_EDGE_STEP_MS;
      sim.step(simMs);
    }
    return simMs;
  };

  sim.paint = (canvas) => {
    const context = canvas.getContext('2d');
    if (image === null) image = context.createImageData(sim.W, sim.H);
    const base = sim.fb();
    const heap = module.HEAPU8;
    for (let y = 0; y < sim.H; y++) {
      for (let x = 0; x < sim.W; x++) {
        const black = heap[base + y * sim.STRIDE + (x >> 3)] & (0x80 >> (x & 7));
        const pixel = black ? INK : PAPER;
        const offset = (y * sim.W + x) * 4;
        image.data[offset] = pixel[0];
        image.data[offset + 1] = pixel[1];
        image.data[offset + 2] = pixel[2];
        image.data[offset + 3] = 255;
      }
    }
    context.putImageData(image, 0, 0);
  };

  sim.setup();
  return sim;
}
