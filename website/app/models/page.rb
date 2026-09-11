Page = Decant.define(dir: "content/pages", ext: "html.erb") do
  include Localizable

  frontmatter :title
  frontmatter :description
  frontmatter :nav_title

  def self.find(slug, locale: I18n.locale)
    locale = locale.to_sym

    all.find { |page| page.locale == locale && page.to_param == slug } ||
      raise(Decant::FileNotFound, "Couldn't find #{name} with 'slug'=#{slug} and 'locale'=#{locale}")
  end

  def to_param
    frontmatter&.dig(:slug)&.to_s.presence || base_slug
  end

  def nav_label
    nav_title.presence || title
  end
end
