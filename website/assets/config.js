// LBAudio website settings.
// Change a value here (on GitHub: open this file, tap the pencil icon, edit, Commit changes)
// and the live site updates within a minute or two.
window.LBAUDIO_CONFIG = {
  // The support email shown on every page.
  supportEmail: "support@lbaudio.co.uk",

  // One entry per product. "payhipProduct" is the code after "payhip.com/b/" in its Payhip link.
  // While a product's code is empty, its Buy buttons say "Coming soon" and its label says "Coming soon".
  // Prices here are only what the website shows: set the real price in Payhip too.
  products: {
    squeeze: { payhipProduct: "m2aWH", price: "\u00a315" },
    carve:   { payhipProduct: "fbuws", price: "\u00a315" },
    ember:   { payhipProduct: "JuUOz", price: "\u00a315" },
    haze:    { payhipProduct: "meyHn", price: "\u00a315" },
    ceiling: { payhipProduct: "drU5n", price: "\u00a315" },
    // The second range. Paste each Payhip code in when the product is set up; until then the site says "Coming soon".
    drift:   { payhipProduct: "", price: "\u00a315" },
    hush:    { payhipProduct: "", price: "\u00a315" },
    spread:  { payhipProduct: "", price: "\u00a315" },
    snap:    { payhipProduct: "", price: "\u00a315" },
    orbit:   { payhipProduct: "", price: "\u00a315" },
    // Bundles. Their savings on the website are worked out from the prices above automatically.
    // "bundle" is the core 5 (its Payhip code stays the same).
    bundle:   { payhipProduct: "z0NFa", price: "\u00a360", includes: ["squeeze", "carve", "ember", "haze", "ceiling"] },
    fxbundle: { payhipProduct: "", price: "\u00a360", includes: ["drift", "hush", "spread", "snap", "orbit"] },
    complete: { payhipProduct: "", price: "\u00a3110",
                includes: ["squeeze", "carve", "ember", "haze", "ceiling", "drift", "hush", "spread", "snap", "orbit"] }
  }
};
