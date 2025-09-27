# salt hydrate latent heat store

Latent heat stores are well-known in the form of ice packs.  Melting ice
consumes a lot of energy, therefore ice packs can keep things cool for a long
time.  Great for a mobile fridge.  You could also create a cooling vest by
stashing a few ice packs in the vest pockets.  But 0°C is a bit cold for
comfort.  So, can we find something that melts closer to human comfort
temperature?

Salt hydrates fit the bill.  We have several candidates between 0°C and 36°C:
- 29°C CaCl₂⋅6H₂O (Road Salt)
- 32°C Na₂SO₄⋅10H₂O (Glauber Salt)
- 34°C Na₂CO₃⋅10H₂O (Washing Soda)

There are a few more candidates.  But once you discard the toxic, expensive and
explosive ones, you should focus on those three.  In particular on the last two.

## eutectic mixture

Mixing Glauber and Soda reduces the melting point.  A eutectic mixture appears
to melt at 25°C, which I consider almost ideal for something like a cooling
vest.

A 3-way eutectic with added table salt (NaCl) should reduce the melting point
even further.  But there isn't much data to be found and I don't think this
would be particularly useful anyway.  If you want a melting point near 0°C, just
use water ice.

## Incongruent melting

Salt hydrates have the annoying behavior of not fully dissolving when melted.
Only about 50-80% of the anhydrous salt dissolves in water at the melting point.
The remainder tends to precipitate at the bottom.  When refreezing, you tend to
get a salt layer at the bottom, a water layer at the top and a diminishing layer
of salt hydrate in between.

There are various options of dealing with this problem.  Smaller containers
reduce the distance between salt-layer and water-layer.  Mechanical movement
mixes the components, something that comes fairly naturally in wearable
applications.  Gelling agents reduce separation.  Surplus water reduces the salt
layer while increasing the water layer.

## Super-cooling

Salt hydrates lean towards super-cooling.  Heat packs explicitly use this as a
feature.  They freeze at 58°C, but will super-cool down to 0°C.  Once you
initiate freezing by twisting the little metal inside the pack, it quickly
freezes and temperature increases to 58°C.

For a cooling pack, super-cooling is more of a nuisance.  We should add some
nucleating agent to prevent it.  Borax seems to work well and is cheap.

## My recipes

For a 500ml container, the following works well:
- 142g Na₂SO₄
- 106g Na₂CO₃
- 5g Na₂B₄O₇
- 360g H₂O

This is 1 mole each of Na₂SO₄⋅10H₂O and Na₂CO₃⋅10H₂O and almost perfectly fits a
500ml bottle.  I don't think you need 5g of Borax.  As long as a tiny bit of it
remains undissolved, it should work fine as a nucleating agent.  Maybe we can
reduce it to 1g, I haven't tried that yet.

Incongruent melting was still a bit of a problem and I didn't want to deal with
gelling agents while experimenting.  So I tried using a little more water.

- 121g Na₂SO₄
- 90g Na₂CO₃
- 5g Na₂B₄O₇
- 428g H₂O
- 382g H₂O

Idea is to use 0.9 mole each of Na₂SO₄⋅10H₂O and Na₂CO₃⋅10H₂O, then fill the
bottle to the top.  This recipe I am quite happy with.

## Results

I can keep a cooling pack in direct contact with my skin without it being too
uncomfortable.  It will take 8h or longer to fully melt.  Good enough for a
little hobby project.  As a commercial product, smaller flat containers in a
custom vest would obviously be better.

Freezing is rather sluggish at ambient temperatures.  Keeping the pack outside
overnight barely has any effect.  Inside a fridge it will fully freeze in 8h or
less.
