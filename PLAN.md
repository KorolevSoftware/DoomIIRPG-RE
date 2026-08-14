# Doom II RPG — план переписывания (new_src)

Порт Doom II RPG (J2ME → C++) в `new_src/` на свежей системе:
GL 3.3 core + шейдеры, канвас 480x320 (letterbox), CMake.

Референс (legacy): `src/` (TinyGL софт-растеризатор + GLES OpenGL-путь).
Решение по 3D: **растеризатор TinyGL НЕ портируем** — портируем GL-путь
(`Render::drawNodeGeometry`/`DrawModelVerts`/`RasterizeConvexPolygon`)
на новую GL 3.3-систему текущей переписи.

## План

- [x] Фаза 1. Каркас приложения: AppContext, архив .ipa, окно, GL-контекст,
      RenderBackend (бэтч, шейдеры), тестовая отрисовка канваса.
- [x] Фаза 2. HUD/2D: Font.bmp, Graphics2D, Hud (кокпит, оружие, здоровье,
      монстры, пузыри текста, стрелки, vignette урона, баннеры).
- [x] Фаза 2.5. Медиа: newMappings.bin + newPalettes.bin + newTexels*.bin
      → MediaLoader (палитры/тексели, registerMedia/finalizeMapMedia).
- [x] Фаза 2.6. Карта: MapParser полный разбор mapXX.bin в MapData
      (геометрия, нормalи, BSP-узлы, линии, спрайты, tileEvents, байткод,
      maya-камеры, флаги). Верификация по tools/map_to_obj.py.
- [x] Фаза 2.6a. Декодирование геометрии: decodePolys (leaf-узлы,
      1787 полигонов map00 — совпадает с эталоном), format вертексов.
- [x] Фаза 2.7. Graphics2D::drawLine + 2D-миникарта (каркас полигонов)
      — центрирование исправлено, пользователь подтвердил.
- [x] Фаза 3. 3D-рендер карты (GL-путь) на GL 3.3:
      - [x] Камера: порт buildViewMatrix/buildProjectionMatrix/multMatrix
        (14.14 fixed → float), BeginFrame-модификации projection → Camera3D.
      - [x] 3D-шейдер (MVP + indexed-текстура через палитру), drawModelVerts
        → World3D (фан-триангуляция, VERT_COORDS_TO_FLOAT /16384,
        TEXT_COORDS_TO_FLOAT /1024, GL_REPEAT для тайловых текстур).
      - [x] Загрузка media-текстур (mediaMappings[tileNum] → texel/palette)
        → uploadMapTextures. Пользователь подтвердил: текстурированные
        полигоны, пиксельная текстура (GL_NEAREST) — корректно.
      - [x] Обход BSP (walkNode, nodeClassifyPoint, cullBoundingBox) —
        порядок отрисовки листьев. drawBSP в World3D: walkNode(0) собирает
        видимые листья, рисует в обратном порядке (дальние→ближние,
        painter's). nodeClassifyPoint — знак (view·normal)+offset.
      - [ ] drawNodeGeometry + faceCull/swapXY/expandEdgePoly (расширение
        2-вертексных граней уже в MapParser; GL-путь рисует как есть).
      - [x] fog/скрин-эффекты: World3D::setFog (fogColor ARGB, fogMin,
        fogRange; alpha==0 → выкл; fogScale=1/8000; legacy swap R/B),
        шейдер: uView → eye-depth, линейный fog по RGB (alpha не трогаем,
        чтобы прозрачные пиксели билбордов не «туманились» в квадрат).
        Пользователь подтвердил: туман красивый, альфа корректна.
      - [x] Небо (DrawSkyMap/skyMap): uploadSky из tables.bin (skyPaletteA/
        skyTexelA для map00, skyIndex=((mapID-1)/5%2)*2), drawSky —
        полноэкранный NDC-квад, UV = (ndc*0.5, -ndc.y*0.5+0.5) - yaw/256,
        identity MVP. Пользователь подтвердил: небо корректно.
      - [x] Спрайты-билборды (renderSpriteObject/renderStreamSpriteGL):
        uploadSpriteTextures (RLE-декод column-RLE, bounds), drawSprite —
        билборды (raw bounds / RLE 518·1036 + 176-crop) и настенные декали
        (Wall-ветка с viewStepValues, терминалы/порталы z-коррекции);
        привязка спрайтов к BSP-листам (getNodeForPoint, высота как в
        postProcessSprites), сортировка по mvp-глубине; ящики z-=224.
        Без depth buffer (как GLES::SetGLState), painter's алгоритм.
        viewSin/viewCos — из sin-таблицы, НЕ из view-матрицы.
      - [x] Реальная камера вместо автовращения: WASD/стрелки (движение
        вперёд = +cos*K,-sin*K, стрейф, поворот ←/→), ESC — выход.
- [ ] Фаза 4. Игровые сущности: Player, Entity, инвентарь, оружие,
      table.bin (attacks/weapons/stats), entities.bin.
- [ ] Фаза 5. Игровой цикл: миры, локации, tileEvents, скрипты, диалоги,
      combat.

## Статус (заметки для разработки)

- Сборка: `cmake --build build_new -j 8`. Проект на GLOB — при добавлении
  новых .cpp/.h нужен переконфиг: `cmake -S . -B build_new`.
- Запуск: `cd build_new/new_src && ./DoomIIRPG`. При redirect в файл stdout
  буферизуется, stderr пишется сразу.
- Архив: `build_new/new_src/Doom 2 RPG.ipa`, чтение через AppContext.
- Пользователь — «глаза»: изображения не смотреть скриптами, спрашивать,
  что видно на экране.

## Формат mapXX.bin (проверен на map00.bin, 75 997 байт)

- Заголовок 42 байта (version=3, compileDate, spawnIndex, spawnDir,
  flags, secrets, loot, numNodes, dataSizePolys, numLines, numNormals,
  numNormalSprites, numZSprites, numTileEvents, mapByteCodeSize,
  totalMayaCameras, totalMayaCameraKeys, 6×offMayaTween, 0xDEADBEEF).
- 0xDEADBEEF → mediaCount u16 → mediaCount×u16 → 0xDEADBEEF.
- Секции с маркером 0xCAFEBABE: normals (numNormals×3×s16),
  nodeOffsets (numNodes×s16), nodeNormalIdxs (numNodes×u8),
  nodeChildOffset1/2 (numNodes×s16), nodeBounds (numNodes×4×u8),
  nodePolys (dataSizePolys×u8), lineFlags ((numLines+1)/2),
  lineXs/lineYs (numLines×2), heightMap (1024×u8).
- Спрайты: mapSprites 10 полей (X,Y,Z,NODE,RENDMODE...), mapSpriteInfo.
- staticFuncs 12×u16 → tileEvents (numTileEvents×2×i32) → mapByteCode →
  mayaCameras (numKeys u8, sampleRate i16, keys 7×s16, tweenIdx 6×s16,
  counts 6×s16 c clip max(0,..), DEADBEEF, tweens) → CAFEBABE →
  512 байт флагов → CAFEBABE.

map00: nodes=695 lines=513 normals=23 polys=47628 sprites=135(+126 z)
tileEvents=167 bytecode=10015 mayaCams=14 mayaKeys=86 mayaTweens=937
decodedPolys=1787 (совпадает с tools/map_to_obj.py: 2300 verts/1787 polys).

## Формат полигонов (drawNodeGeometry, Render.cpp:931)

- mesh: u16 packed = raw[o+4]|raw[o+5]<<8 → textureId=packed>>7,
  polyCount=packed&0x7F; polyFlags u8 → numVerts=(flags&7)+2,
  axis=flags&24 (X=0, Y=8, Z=16), UV_DELTAX=128.
- vertex: x=(b0&0xFF)<<7, y=(b1&0xFF)<<7, z=(b2&0xFF)<<7,
  s=(int8)b3<<6, t=(int8)b4<<6.
- 2-вертексные грани → квад (expandEdgePoly, Render.cpp:997-1041).
- Спец-текстуры: HELL_HANDS → RENDER_BLEND50, FADE/SCORCH → SUB (TinyGL)
  или NORMAL (GL), FLAT_LAVA/LAVA2 → faceCull=NONE + анимация UV.

## Формат maya-камер (Game::loadMayaCameras, Game.cpp:586)

- per cam: numKeys u8, sampleRate i16, keys numKeys×7×i16,
  tweenIndices numKeys×6×i16, counts 6×i16 (clip max(0,..)),
  DEADBEEF, n3=sum(counts) байт tweens; после всех камер CAFEBABE.

## GL-путь legacy (для порта в Фазу 3)

- TinyGL::setView (TinyGL.cpp:178): buildViewMatrix + buildProjectionMatrix
  + multMatrix → mvp; BeginFrame(viewport, view, projection).
  setViewPort сдвигает posY (GL: posY=3 при !chatZoom).
- BeginFrame (GLES.cpp:111): viewport/scissor, конвертация 14.14→float,
  модификации projection: отрицание [4],[8],[1],[5],[9], [14]*=0.5
  (если !chatZoom), fog fogStart/fogEnd.
- drawModelVerts (TinyGL.cpp:223): если GL-путь (isInit) →
  _gles->DrawModelVerts (мировые координаты, w=1, st=texel/1024),
  иначе софт-путь (faceCull + transform3DVerts + ClipQuad).
- RasterizeConvexPolygon (GLES.cpp:299): identity-проекция, вертексы
  clip-space (после transform3DVerts) → деление на w на CPU,
  glDrawElements (фан 0,1..n). Для sky — st по NDC + yaw.
- Render::setupTexture (Render.cpp:2043): mediaIdx=mediaMappings[tileNum]+frame,
  texIdx=mediaTexelSizes[mediaIdx]&0x3FFF, palIdx=mediaPalColors[mediaIdx]&0x3FFF;
  sWidth=1<<widthBits и т.д.
- Render::renderBSP (Render.cpp:1710): walkNode(0) собирает видимые
  nodeIdxs, затем для каждого листа drawNodeGeometry + спрайты.
- walkNode (Render.cpp:1049): cullBoundingBox, лист→drawNodeLines+спрайты,
  иначе nodeClassifyPoint определяет порядок детей (ближний/дальний).
- nodeClassifyPoint (Render.cpp:1099): знак (view·normal)+nodeOffsets.
- Координаты: вертекс байт<<7 (128 ед = 2 тайла), карта 32x32 тайла = 2048.

## Матрицы (14.14 fixed, TinyGL.cpp)

- buildViewMatrix (строки 68-98): sinTable[yaw/pitch/roll]>>2 →
  ортонормированная view; MATRIX_ONE=16384; m[15]=16384.
- buildProjectionMatrix (100-119): n3=aspect>>1, n4=(fov<<14)/aspect,
  n5/n6 из sinTable, m[0]=(n6<<14)/(n4*n5>>14), m[5]=(n6<<14)/n5,
  m[10]=-MATRIX_ONE, m[14]=-(2*NEAR_CLIP=256), m[11]=-MATRIX_ONE, m[15]=0.
- multMatrix (121-127): dest[i*4+j] = Σ(m1[i*4+k]*m2[k*4+j])>>14.

## Константы

- TinyGL: UNIT_SCALE=65536, MATRIX_ONE=16384, NEAR_CLIP=256,
  CULL_EXTRA=NEAR_CLIP+16, SCREEN_SHIFT=3, SCREEN_ONE=8,
  COLUMN_SCALE_INIT=INT_MAX.
- float-конверсия (GLES.cpp): VERT_COORDS_TO_FLOAT=x/16384,
  TEXT_COORDS_TO_FLOAT=x/1024, COLOR_BYTE_TO_FLOAT=x/256,
  YAW_TO_FLOAT=x/256.

## Маппинг media

- MediaMappings::mappings — это mediaMappings legacy (kMaxMappings=512/32768
  shorts, mediaId). В map геометрия textureId=tileNum →
  mediaIdx=mediaMappings[tileNum]; texelIndexFor/paletteIndexFor по mediaId.
- Texture (new_src) уже умеет indexed (R8 + RGBA8 палитра LUT 256x1),
  transparent=0xF81F, CLAMP_TO_EDGE; добавлен параметр `repeat`
  (GL_REPEAT) для тайловых 3D-текстур.

## Фаза 3 — сделано (3D на GL 3.3)

- Camera3D: buildView/buildProjection/multMatrix (14.14 fixed→float),
  mvpF_[16] column-major; BeginFrame-модификации projection в setView.
  Передаётся как uMVP; вертексы/UV делятся на CPU (VERT/TEXT_COORDS_TO_FLOAT).
- World3D: MVP-шейдер (aPos vec3 + aUV vec2, uMVP), indexed-фрагмент
  (R8 index + RGBA8 палитра LUT), фан-триангуляция полигона, загрузка
  текстур по textureId (media.mappings()[tile] → texel/palette, repeat=true).
- Интеграция в Main.cpp: камера в спавне map00 (spawnIndex=612 → тайл (4,19),
  camX/camY=(tile*64+32)<<4, camZ=(height+36)<<4, fov=290,
  aspect=(290<<14)/((480<<14)/320), yaw=spawnDir<<7), автовращение для проверки.
- BSP-обход (World3D::drawBSP): walkNode рекурсивно, cullBoundingBox —
  упрощённый 2D-reject по позиции камеры (полный проекционный cull не нужен
  для GL, painter's достаточно); nodeClassifyPoint портирован точно; рисует
  nodeIdxs в обратном порядке (дальние листья первыми).
- Баг: World3D оставлял VAO не-привязанным → GL_INVALID_OPERATION в
  SpriteBatch.flush. Фикс: glBindVertexArray(vao_) в начале flush().
- Баг распаковки текстур: MediaLoader::finalize хранил только registered
  texel/palette и индексировал по registered-счётчику, а legacy/python
  (tools/extract_textures.py) нумеруют ВСЕ non-reference записи подряд.
  Из-за этого texelIndexFor/paletteIndexFor давали неверные индексы →
  текстуры «шум». Фикс: finalize читает все non-ref записи (позиция
  продвигается на size+4, переключение файла при >0x40000) и заполняет
  texelIndex_/paletteIndex_ порядковым индексом store. Верифицировано:
  tile126 (blood_splatter) и tile258 (wall) побайтово совпадают с эталоном.
- Также: размеры текстур из dimensions — это биты степени двойки
  (w=1<<(dims>>4), h=1<<(dims&0xF)), а не сами значения (было 7x7 вместо
  128x128). Прозрачность 0xF81F применяется ко ВСЕМ текстурам (не только
  спрайтам), как в legacy CreateTextureForMediaID.
- Отладка: BMP-дампы текстур сохраняются в tex_dump_tileXXX_*.bmp рядом с
  исполняемым файлом.