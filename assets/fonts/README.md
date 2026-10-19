# Local font resources

`RaceEngineerSans-{Regular,Medium,SemiBold}.ttf` are static instances generated from the existing `IBMPlexSans-VF.ttf` source (SHA-256 `3b031aa4216174205bd8471f88a49b91f093169e9e87bd5262242bc5967fe2e3`). The source axes are `wght` 100–700 (default 400) and `wdth` 75–100 (default 100). Instances were created locally with fontTools 4.61.1 at `wdth=100` and weights 400, 500, and 600.

The derivative family is named **RaceEngineer Sans** to comply with the source OFL's reserved font name “Plex”. `raceengineer-sans-OFL.txt` retains the upstream copyright and license. The original variable source remains in this directory; the app packages the three static instances.

Qt 6.8.3 registers these as distinct Regular, Medium, and SemiBold styles. The bundled Barlow Condensed face is the original SemiBold file; display text requests Bold for a heavier render. IBM Plex Mono remains the original Regular face and is used for data values with Qt's Bold weight request.
