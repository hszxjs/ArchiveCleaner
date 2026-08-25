# Third-Party Licenses

This project uses the following open-source software. We gratefully acknowledge their authors.

## EUI-NEO (Apache License 2.0)

- **Project**: [EUI-NEO](https://github.com/sudoevolve/EUI-NEO)
- **Author**: sudoevolve
- **License**: Apache License 2.0
- **Usage**: GPU-accelerated UI framework (GLFW + OpenGL backend)
- **Source**: Bundled in `third_party/eui-neo-src/`

```
Copyright 2024-2026 sudoevolve

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0
```

## GLFW (zlib License)

- **Project**: [GLFW](https://www.glfw.org/)
- **Author**: Camilla Löwy
- **License**: zlib License
- **Usage**: Window and input backend (bundled in EUI-NEO)

## FreeType (FTL / GPL-2.0 dual license)

- **Project**: [FreeType](https://freetype.org/)
- **License**: FreeType License (BSD-like) OR GPL-2.0
- **Usage**: Font rendering (bundled in EUI-NEO)

## libpng (PNG Reference Library License v2)

- **Project**: [libpng](http://www.libpng.org/)
- **License**: PNG Reference Library License version 2
- **Usage**: PNG image decoding (bundled in EUI-NEO)

## zlib (zlib License)

- **Project**: [zlib](https://www.zlib.net/)
- **Authors**: Jean-loup Gailly and Mark Adler
- **License**: zlib License
- **Usage**: Compression (bundled in EUI-NEO)

## yyjson (MIT License)

- **Project**: [yyjson](https://github.com/ibireme/yyjson)
- **Author**: YaoYuan
- **License**: MIT License
- **Usage**: JSON parsing (bundled in EUI-NEO)

```
Copyright (c) 2020 YaoYuan <ibireme@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files...
```

## md4c (MIT License)

- **Project**: [md4c](https://github.com/mity/md4c)
- **Author**: Martin Mitáš
- **License**: MIT License
- **Usage**: Markdown parsing (bundled in EUI-NEO)

## stb_image (Public Domain / MIT)

- **Project**: [stb](https://github.com/nothings/stb)
- **Author**: Sean Barrett
- **License**: Public Domain (MIT fallback)
- **Usage**: Image loading (bundled in EUI-NEO)

## nanosvg (zlib License)

- **Project**: [nanosvg](https://github.com/memononen/nanosvg)
- **Author**: Mikko Mononen
- **License**: zlib License
- **Usage**: SVG rendering (bundled in EUI-NEO)

## tray (MIT License)

- **Project**: [tray](https://github.com/zserge/tray)
- **Author**: Serge Zaitsev
- **License**: MIT License
- **Usage**: System tray support (bundled in EUI-NEO)

## glad (MIT License)

- **Project**: [glad](https://github.com/Dav1dde/glad)
- **Author**: David Herberth
- **License**: MIT License
- **Usage**: OpenGL loader (bundled in EUI-NEO)

---

## Bundled Fonts

以下字体文件随程序分发（`assets/` 目录）。

### Font Awesome 7 Free — Solid 900 (SIL OFL 1.1 / CC BY 4.0)

- **Project**: [Font Awesome](https://fontawesome.com/)
- **Author**: Fonticons, Inc.
- **License**: 字体文件采用 SIL Open Font License 1.1；图标图形采用 CC BY 4.0
- **Usage**: 图标字体（bundled in EUI-NEO assets）

### 荆南君君体 (JingNanJunJunTi)

- **Project**: 荆南君君体（作者：荆南君君）
- **License**: 作者公开发布的免费商用授权（可免费用于个人和商业项目，禁止单独出售字体文件）
- **Usage**: 默认 UI 文字字体（bundled in EUI-NEO assets）

### 优设标题黑 (YouSheBiaoTiHei)

- **Project**: [优设标题黑](https://www.uisdc.com/uisdc-font)（作者：优设 UISDC）
- **License**: 作者公开发布的免费商用授权（可免费用于个人和商业项目）
- **Usage**: 框架可选标题字体（bundled in EUI-NEO assets）

---

## Additional Tools

The following tools are optionally used at runtime:

### Everything (voidtools)

- **Project**: [Everything](https://www.voidtools.com/)
- **Usage**: File indexing service for instant search
- **Note**: Bundled in installer as optional component

### fd / fdfind (MIT OR Apache-2.0)

- **Project**: [fd](https://github.com/sharkdp/fd)
- **Author**: David Peter
- **License**: Dual licensed under the MIT License and Apache License 2.0
- **Usage**: Fast file finder (search engine option)
