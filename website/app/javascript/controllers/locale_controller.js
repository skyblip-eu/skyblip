import { Controller } from "@hotwired/stimulus"
import { Turbo } from "@hotwired/turbo-rails"

export default class extends Controller {
  static values = { current: String, default: String, available: Array, alternates: Object }

  connect() {
    const locale = this.cookie ?? this.#browserLocale()
    this.cookie = locale

    const url = this.alternatesValue[locale]
    if (locale !== this.currentValue && url) Turbo.visit(url, { action: "replace" })
  }

  choose({ params: { locale } }) {
    this.cookie = locale
  }

  #browserLocale() {
    const languages = navigator.languages ?? [navigator.language]

    return languages
      .filter(Boolean)
      .map(language => language.toLowerCase().split("-")[0])
      .find(locale => this.availableValue.includes(locale)) ?? this.defaultValue
  }

  get cookie() {
    return document.cookie.split("; ").find(c => c.startsWith("locale="))?.split("=")[1]
  }

  set cookie(value) {
    document.cookie = `locale=${value}; path=/; max-age=31536000; SameSite=Lax`
  }
}
