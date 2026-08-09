# Screenshot Checklist

Capture screenshots at 16:9, preferably 1920 × 1080 or 2560 × 1440. Use **Fixed Camera** before making comparison pairs, wait for texture streaming and acceleration-structure construction to finish, and hide the UI with `U` unless the shot specifically demonstrates the controls. Save PNG files without post-processing that changes the renderer output.

## Required README images

1. `bistro-night-ray-query.png` — hero image. Use the Bistro street at night with Ray Query mode, Night preset, shadows, reflections/refraction, IBL and TAA enabled. Frame street lamps, glass, wet/reflective materials, and visible depth in one composition.
2. `bistro-day-raster.png` — raster comparison. Lock the same or a clearly documented camera, use Rasterization + PBR + Forward+ + IBL + GTAO + TAA, and choose the Day lighting setup.
3. `bistro-renderer-ui.png` — interface overview. Keep the scene visible while showing the Renderer and Camera panels, including the mode selector and the TAA/IBL controls.

## Useful optional comparisons

4. `bistro-rayquery-off.png` and `bistro-rayquery-on.png` — identical fixed camera and light settings, changing only the Ray Query effects needed to show contact shadows, reflections, or glass.
5. `bistro-taa-off.png` and `bistro-taa-on.png` — identical camera, preferably with thin geometry, lamp edges, or distant railings. Capture at full resolution rather than cropping a scaled preview.
6. `bistro-ibl-debug.png` — one screenshot of the IBL debug view, or a four-image montage of Environment, Diffuse Irradiance, Specular Prefilter, and BRDF LUT.
7. `wine-glass-refraction.png` — switch the model path in `main.cpp` to `Assets/3d_wine_glass_goblet/scene.gltf`; capture thick-glass absorption and refraction against a background with visible spatial detail.

For every A/B pair, change only the named feature. Avoid moving the camera, changing exposure, or resizing the window between the two captures.
