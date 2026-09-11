module Localized
  extend ActiveSupport::Concern

  included do
    around_action :switch_locale
  end

  private
    def switch_locale(&action)
      locale = params[:locale] || I18n.default_locale
      I18n.with_locale(locale, &action)
    end

    def default_url_options
      return {} if I18n.locale == I18n.default_locale

      { locale: I18n.locale }
    end
end
