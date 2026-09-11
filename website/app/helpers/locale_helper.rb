# View-layer localization for the Localizable content assigned to @page.
module LocaleHelper
  def page_alternate_paths
    @page_alternate_paths ||= page_alternates.transform_values { |page| page_url_for(page, only_path: true) }
  end

  def hreflang_link_tags
    urls = page_alternates.transform_values { |page| page_url_for(page) }
    urls[:"x-default"] = urls[I18n.default_locale]

    safe_join(urls.compact.map { |locale, url| tag(:link, rel: "alternate", hreflang: locale, href: url) }, "\n")
  end

  def page_url_for(page, only_path: false)
    locale = page.locale unless page.locale == I18n.default_locale

    if page.base_slug == "index"
      root_url(locale:, only_path:)
    else
      page_url(page, locale:, only_path:)
    end
  end

  private
    def page_alternates
      return {} unless @page

      @page_alternates ||= I18n.available_locales.index_with { |locale| Page.find_by(base_slug: @page.base_slug, locale:) }.compact
    end
end
