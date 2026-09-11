module Localizable
  extend ActiveSupport::Concern

  class_methods do
    def find_by(base_slug:, locale: I18n.locale)
      locale = locale.to_sym

      all.find { |entry| entry.locale == locale && entry.base_slug == base_slug }
    end
  end

  def locale
    suffix = slug[/\.([a-z]{2,3})\z/, 1]&.to_sym
    I18n.available_locales.include?(suffix) ? suffix : I18n.default_locale
  end

  def base_slug
    slug.delete_suffix(".#{locale}")
  end
end
