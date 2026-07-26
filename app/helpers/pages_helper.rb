module PagesHelper
  def link_to_page(slug, label: nil, label_type: :title, **html_options)
    page = find_page(slug)
    label ||= label_type == :nav ? page.nav_label : page.title

    lang = page.locale unless page.locale == I18n.locale
    link_to label, page_url_for(page, only_path: true), { lang: }.merge(html_options)
  end

  def pages_image_tag(path, options = {})
    image_tag "pages/#{@page.base_slug}/#{path}", options
  end

  private
    def find_page(slug)
      page = Page.find_by(base_slug: slug) || Page.find_by(base_slug: slug, locale: I18n.default_locale)
      raise Decant::FileNotFound, "Couldn't find Page with 'base_slug'=#{slug} and 'locale'=#{I18n.locale}" unless page

      page
    end
end
