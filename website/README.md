# website

[skyblip.eu](https://skyblip.eu). A Rails app that never serves a request in production: [Parklife](https://github.com/benpickles/parklife) crawls it and writes static files, which GitHub Pages serves.

Every command here runs from this directory, not from the repo root.

## Running it

```sh
bin/setup          # gems, then bin/dev
bin/dev            # http://skyblip.localhost:8119
bin/ci             # what CI runs: rubocop, bundler-audit, importmap audit, brakeman
```

## Content

Pages are files, not database rows: `content/pages/<slug>.html.erb`, with YAML frontmatter for `title`, `nav_title`, `description` and an optional localized `slug`. A page is bilingual by convention, `<slug>.html.erb` and `<slug>.fr.html.erb`, and the locale switcher offers a language only where the twin exists.

Parklife discovers pages by crawling from the root, so **a page nothing links to is not built**. Link it from the nav (`app/views/layouts/_nav.html.erb`) or from another page's body.

Look and layout come from the token sets in `app/assets/stylesheets/`. Colors, spacing and type are CSS variables in `_global.css`; components never hardcode a value.

## Building and deploying

```sh
bin/static-build --base https://skyblip.eu   # -> build/
```

`bin/static-build` precompiles assets, runs Parklife, then copies `public/` over the result, which is how anything that must not be fingerprinted gets served verbatim.

Deployment is `.github/workflows/website.yml` at the repo root: a push to `main` that touches `website/` builds and publishes to Pages. Nothing else triggers a deployment, and `workflow_dispatch` forces one.

## License

MIT, see [`LICENSE`](LICENSE). The rest of the repository is GPL-3.0-only.
