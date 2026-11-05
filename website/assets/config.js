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
    carve:   { payhipProduct: "",      price: "\u00a315" },
    ember:   { payhipProduct: "",      price: "\u00a315" },
    haze:    { payhipProduct: "",      price: "\u00a315" },
    ceiling: { payhipProduct: "",      price: "\u00a315" },
    // The bundle's saving on the website is worked out from these prices automatically
    bundle:  { payhipProduct: "",      price: "\u00a349", includes: ["squeeze", "carve", "ember", "haze", "ceiling"] }
  }
};
