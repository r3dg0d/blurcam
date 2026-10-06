# Fixture provenance

`astronaut.png` is a 256×256 resize of the public-domain NASA photograph of astronaut
Eileen Collins provided by scikit-image. No private user image is included.

Source: https://raw.githubusercontent.com/scikit-image/scikit-image/v0.22.0/skimage/data/astronaut.png
License/provenance: https://scikit-image.org/docs/stable/api/skimage.data.html#skimage.data.astronaut
The upstream documentation records no known copyright restrictions and public-domain status.
The resize was produced with FFmpeg's scale filter. No endorsement by NASA is implied.

Synthetic effect fixtures are generated from deterministic pixels in the test suite.

`gradient.ppm` is an independently generated 8×8 RGB gradient: `(16*x,16*y,8*(x+y))`.
`pixelate-expected.ppm` contains analytical 4×4 block averages of that gradient.
These two tiny fixtures are original MIT project assets, not captures of the renderer.
