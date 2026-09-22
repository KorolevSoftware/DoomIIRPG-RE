# Status

_Last updated: 2026-09-22_

## Controls (2026-08-29, aligned with `src/Input.cpp:80-95`)

| Key | Effect |
|---|---|
| UP / W, DOWN / S | move forward / backward (menu + dialog + loot: scroll) |
| LEFT / RIGHT | turn (menu + dialog + loot: page) |
| A / D | turn (deviation: the original strafes; no strafe in the rewrite) |
| RETURN | attack / talk / use, menu select, dialog page advance, loot page |
| C | pass turn (also closes the loot list) |
| TAB | automap (logs only, no screen yet) |
| ESCAPE | open menu / back — **does not quit** |
| BACKSPACE | back (ours; also closes the loot list) |
| K | debug: grant keycards (ours, removable) |

There is no quit key: the window close button is the only exit, as in the
reference. Z/X/I/O/P/B are reserved-unmapped (no rewrite counterpart). Full
rationale: `docs/architecture/specs/2026-08-27-ui-layer.md` §"KEYMAP 2026-08-29".

## Working on

- **ПЕРЕХОД НА SOKOL_GFX (spec 2026-09-11-sokol-gfx-backend.md, ADR-0024/0025/0026).**
  Решение пользователя 2026-09-11: переписать графику на sokol_gfx как
  кроссплатформенную обёртку GPU (Metal на Apple, DirectX на Windows, OpenGL на
  Linux), от бэкенда SDL_Render отказаться — он давал вчетверо больший расход
  памяти. SDL2 ОСТАЁТСЯ окном и вводом, прослойку создания поверхностей GPU
  пишем сами. Шейдеры — через `sokol-shdc` и его интеграцию с CMake
  (`sokol_shaders.cmake`, функция `sokol_shader()`).
  - **Блокирующий вопрос закрыт фактом:** `SG_PIXELFORMAT_R8` поддерживается
    безусловно на GL/GLES3/Metal/WGPU/Vulkan и через запрос возможностей на
    D3D11 (проверено по исходнику sokol с номерами строк). Индексная схема и
    15.1 МБ сохраняются — мотивация ухода от SDL держится.
  - **Четыре находки, сделанные ДО реализации:** (1) sokol не нормализует
    пространство отсечения — GL отсекает по `-w<=z<=w`, Metal/D3D11 по `0<=z<=w`,
    а наша камера выдаёт GL-проекцию → на Metal исчезла бы ближняя половина мира;
    лечится uniform'ом `depth_fix`, тождественным на GL (побайтовый диф с
    эталоном сохраняется). (2) Самая низкая цель shdc — `glsl410`, поэтому
    контекст запрашиваем GL 4.1 core (macOS останавливается ровно на 4.1).
    (3) Запись в буферы посреди кадра запрещена, а мы флашим десятки раз за кадр
    → ADR-0025: список команд на кадр, одна запись, один проход, воспроизведение.
    (4) Буфер глубины не нужен вовсе (порядок задаёт сортировка) — это убирает
    самую тяжёлую часть прослойки под Metal/D3D11.
  - Архитектор не ограничился чтением: написал оба шейдера на диалекте shdc,
    скачал компилятор и собрал под glsl410/glsl300es/metal/hlsl5/wgsl/spirv_vk —
    ноль диагностик. Имена и диалект в спеке получены, а не предположены.
  - **F12 становится доступен только на GL** (ADR-0026): у sokol нет чтения
    обратно, на Metal вывод происходит внутри `sg_commit`. Сравнение кадров живёт
    на конфигурации `glcore`, которая остаётся ПОСТОЯННО поддерживаемой, а не
    костылём на время работ.
  - **ADR-0022 (туман зависит от бэкенда) лишается предмета** — туман снова
    попиксельный на всех бэкендах. ADR-0021/0022/0023 и спека тесселяции
    2026-09-07 переведены в исторические.
  - **G1 СДЕЛАНА И ПРОВЕРЕНА ПОЛЬЗОВАТЕЛЕМ (2026-09-11):** `render/sdl/` удалён
    целиком (1737 строк против 36 добавленных), цель `dr_render_sdl`, вариант
    бэкенда `sdl`, ветка `GraphicsApi::SdlRender` — всё убрано; вместе с ними
    умерли тесселяция G7.1 и отброс за-экранной геометрии (в sokol перспективная
    коррекция аппаратная). Контекст переведён на GL 4.1 core — ПРАВИТЬ НИЧЕГО НЕ
    ПРИШЛОСЬ: шейдеры версии 330 законны в 4.1 core, расширений совместимости не
    использовали, а на macOS строка версии не изменилась вообще (Apple и раньше
    отдавала 4.1 на запрос 3.3). Трассы побайтово идентичны на 234 строках,
    расход памяти тот же байт в байт.
    Эталонная сборка предыдущей версии оставлена в рабочем дереве git
    (`<scratchpad>/ref`) — снимки F12 сравниваются с работающим бинарником, а не
    с памятью.
  - **G2 СДЕЛАНА (2026-09-11):** sokol вендорен заголовком (1.3 МБ), инструмент
    шейдеров — git-подмодулем (`shallow`, закреплён `11d0cf67`; в историю идёт
    613 байт вместо 53 МБ пребилдов). CMake падает внятно, если подмодуль не
    инициализирован (проверено убиранием каталога). Шейдеры реально
    генерируются в сборку, в `new_src/` ничего не пишется.
    **NB при клонировании нужен `git submodule update --init --recursive`.**
    `--backend=sokol` даёт чёрное окно без ошибок sokol; путь по умолчанию — gl.
    Замеченные 11 FPS на пустом кадре — НЕ дефект фундамента: заглушка
    хранилища возвращает недействительные id, кэш спрайтовых текстур в
    `World3D` наполняется только при успешной загрузке, поэтому каждый спрайт
    распаковывается из RLE каждый кадр (89 мс вне бэкенда; все вызовы sokol
    вместе 0.04 мс). Лечится САМО в G3 — доказано временной правкой: 62 FPS.
    Моя гипотеза про буфер uniform-ов опровергнута: в GL-пути sokol это поле
    не читается вовсе.
  - **G3 СДЕЛАНА (2026-09-11):** настоящее хранилище текстур sokol. Память
    совпала С GL ДО БАЙТА (15810037 у обоих), аномалия 11 FPS исчезла (62 у
    обоих) — проверено мной лично. Индексная схема без разворота в RGBA;
    палитра идёт через тот же общий `expandPalette565`, что и GL, поэтому ключ
    прозрачности не может разойтись между бэкендами.
  - **G4 СДЕЛАНА И ПРИНЯТА ПОЛЬЗОВАТЕЛЕМ (2026-09-12):** кадровый список команд
    (ровно ОДНА запись буфера на кадр, ADR-0025), 4 программы x 4 варианта
    смешивания, двумерный слой. Снимок меню ПОБАЙТОВО совпал с GL: 0
    различающихся пикселей из 614400, при 601256 не-чёрных (сравнение непустое).
    Приём: снятие кадра привязано к СЧЁТУ ТАКТОВ, а не ко времени — качание
    курсора меню есть функция от `upTimeMs = ticks*15`, поэтому на одинаковом
    такте фаза одинакова и расхождение схлопывается в ноль (в прошлом цикле
    GL против SDL оставалось пятно 13x9 именно из-за фазы).
    Отмечено как незаметное сейчас, но реальное: вертикальное начало координат и
    формула ножниц совпадают с GL только при ЦЕЛОМ масштабе леттербокса (сейчас
    ровно двойной); при дробном верхняя кромка обрезки может разойтись на пиксель.
  - **G5 СДЕЛАНА И ПРИНЯТА ПОЛЬЗОВАТЕЛЕМ (2026-09-12):** мир, небо и туман на
    sokol; **sokol стал путём по умолчанию**, `--backend=gl` — эталон до G8.
    Побайтовое совпадение игрового кадра с GL на тактах 200/400/500 (0 из 614400
    при 608973 не-чёрных); туман проверен отдельно и тоже дал ноль.
    `depth_fix` на glcore — точное тождество (доказано препроцессингом).
    **Найден настоящий дефект:** индексную текстуру и палитру сэмплировали одним
    сэмплером с намоткой, поэтому индекс 255 читал запись палитры 0 — 818
    пикселей в 120 кучках. В GL два объекта текстуры со своими параметрами
    (намотка на индексах, зажим на палитре). Отдельный `smp_pal` дал 0.
    Дефект жил уже в G3/G4 и был невидим в меню: там нет тайлящихся индексных
    текстур. Побайтовое сравнение мира его и выцепило.
    **Цена:** sokol дороже на ~2.4 мс за кадр (11.1 против 8.6 в Debug, ~28%);
    оба упираются в ограничитель ~66 FPS, причина не проверена.
    **Побочно найдена недетерминированность ИГРЫ:** на такте 60 два прогона
    одного и того же GL-бэкенда расходятся на 139 пикселей (x 105..305,
    y 338..402) — что-то в раннем кадре привязано к настенным часам; на тактах
    200/400/500 GL против GL даёт ноль. Отдельная задача.
  - **G6 СДЕЛАНА И ПРИНЯТА ПОЛЬЗОВАТЕЛЕМ (2026-09-22):** окружение Metal
    (`SgEnvironmentMetal.mm`). Сборка Metal: `build_metal/`, конфигурируется
    `-DDOOM2RPG_SOKOL_BACKEND=metal`. `depth_fix` на Metal = (0.5, 0.5) —
    доказано, ремап есть в сгенерированном MSL. Найден и исправлен скрытый
    дефект: `SgEnvironment::resize()` никто не вызывал (на Metal размер
    поверхности застыл бы на стартовом). Буфер uniform-ов 4 МБ по измерению
    (пик 1454 отрисовки × 512 Б ≈ 744 КБ).
    Картинка Metal ПОБАЙТОВО совпадает с GL и sokol-glcore (0 из 614400 на
    кадрах 200 и 400). Ощущение «на OpenGL чуть лучше» — темп, а не картинка:
    glcore 65 FPS ровно, Metal 62-64, тройная буферизация у Metal.
  - **ОТКРЫТЫЕ РЕШЕНИЯ ПОЛЬЗОВАТЕЛЯ по Metal:** (1) Metal по умолчанию на Apple —
    сейчас НЕ переключено, остаётся sokol+glcore (одна строка в
    `new_src/CMakeLists.txt`); (2) эксперимент `maximumDrawableCount = 2` против
    вязкости отклика — нужна проверка движением; (3) оставить ли снятие кадра по
    номеру через переменную окружения (14 строк в `GameLoop.cpp`, откачено).
  - **Сборка после обновления macOS:** CMake кэшировал путь к удалённому
    `MacOSX26.5.sdk`. Обе сборки переконфигурированы с неверсионным
    `$(xcrun --show-sdk-path)`. На новой машине передавать
    `-DCMAKE_OSX_SYSROOT="$(xcrun --show-sdk-path)"`.
  - **G7 (DirectX / D3D11) — ОТЛОЖЕНА ПО РЕШЕНИЮ ПОЛЬЗОВАТЕЛЯ (2026-09-22)**, см.
    пункт в «Known bugs (deferred)». Работающий путь сейчас — sokol поверх
    OpenGL (`build_new`, `glcore`), он же по умолчанию.
  - **G8 СДЕЛАНА И ПРИНЯТА ПОЛЬЗОВАТЕЛЕМ (2026-09-22): эталонный `render/gl/`
    удалён** (13 файлов, 1446 строк; цель `dr_render_gl`; выбор бэкенда). Графика —
    ОДИН бэкенд sokol_gfx: по умолчанию поверх OpenGL, Metal отдельной сборкой.
    **Переход на sokol завершён, кроме отложенной G7.**
  - **ЭТАЛОН ТЕПЕРЬ — ЗОЛОТЫЕ КАДРЫ**, `tests/golden/frames/` (такты 200/400/500 —
    ролик, 750 — игра с HUD, 850 — меню; сняты с сырого GL до его удаления).
    Сверка из командной строки, без F12:
    `DOOM2RPG_CAPTURE_TICKS=200,400,500,750,850 DOOM2RPG_MENU_TICKS=600,700,800
    ./DoomIIRPG`, затем `python3 tools/compare_frames.py --golden <каталог>`.
    Любая правка рендера сверяется этим. Такты 60/120 не использовать —
    недетерминированность самой игры.
  - `--backend=` больше ни на что не влияет (принимается ради старых командных
    строк).
  - (исходная формулировка) G3 (хранилище
    текстур — расход должен совпасть с GL до байта), G4 (кадровый список команд,
    конвейеры, двумерный слой — побайтово идентичный снимок меню), G5 (мир, небо,
    туман; затем sokol становится путём по умолчанию), G6 (Metal), G7 (DirectX),
    G8 (удаление `render/gl/`).
  - Предупреждение для G4 от архитектора: математику ножниц копировать БУКВАЛЬНО
    из `GlDraw2D::applyScissor` (там `lroundf`), а не переиспользовать
    `CanvasViewport::canvasSubRect` (там усечение) — разница в один пиксель дала
    бы ложное расхождение при сравнении кадров.

- **Выделение графического бэкенда (spec 2026-09-02-render-backend-split.md,
  ADR-0020/0021/0022) — РЕШЕНИЯ ПОЛЬЗОВАТЕЛЯ ПРИНЯТЫ 2026-09-07, начата G1.**
  - **Трек A — остаёмся на SDL2.** Откат к SDL3 возможен позже, если результат
    окажется плохим; цена отсрочки нулевая, т.к. `TextureStore::createIndexed`
    говорит индексами + палитрой, никогда RGBA → переход = УДАЛИТЬ разворот
    внутри `SdlTextureStore`, а не переделывать интерфейс.
  - **Все текстуры разворачиваются в 8888** (решение пользователя, не
    рантайм-переговоры). ~40 МБ видеопамяти против 9.7 МБ индексных данных.
  - Измерено пробами в скрэтчпаде (`sdl_fmt_probe.c`, `sdl3_fmt_probe.c`):
    SDL2 2.32.10 отклоняет `INDEX8` на всех драйверах («Palettized textures are
    not supported»); 16-битных форматов С АЛЬФОЙ не заявляет НИ ОДИН драйвер;
    запрос неподдерживаемого формата ведёт к промежуточной поверхности +
    конверсии, т.е. памяти БОЛЬШЕ. SDL3 3.4.0 наоборот поддерживает `INDEX8`
    нативно на metal/opengl/gpu/software и имеет `SDL_SetTexturePalette` —
    отсюда и возможность отката.
  - Туман на SDL-пути — подкраска вершин (ADR-0022, зависит от бэкенда).
    Палитровый туман НЕ обещан: требует замера из §4.2.3.
  - Афинное искажение текстур в SDL-пути принято осознанно (ADR-0021): нет
    компоненты w у вершин `SDL_RenderGeometry` ни в SDL2, ни в SDL3.
  - 7 групп, каждая оставляет игру собираемой и играбельной. G3 — чистка 72
    GL-вызовов в `World3D`. Переключение `--backend=` и снятие кадра обоими
    появляются в G4, т.е. сравнивать картинку можно до готовности SDL-пути.
  - **Найденный по пути баг — ИСПРАВЛЕН в G2:** `Graphics2D::setBlendMode(1)`
    означал ADD50 в `SpriteBatch`, но `RENDER_BLEND25` в мировой таблице — две
    несовместимые нумерации одного понятия. Осталась одна.
  - **СДЕЛАНО И ПРОВЕРЕНО ПОЛЬЗОВАТЕЛЕМ (2026-09-07): G1, G2, G3 + модуляция.**
    G1 — ядро `render/api/` + цели `dr_render_core`/`dr_render_gl`.
    G2 — `GlTextureStore`/`GlDraw2D`/`GlRenderBackend`, GL в двумерном слое = 0;
    первым шагом разведены одноимённые `Texture`/`RenderBackend`.
    G3 — `GlScene3D` наполнен, `World3D` 1577→1330 строк, GL = 0, мост `glTexOf`
    удалён, GL больше не протекает в `ui/`.
    Модуляция цвета включена в двумерном пути: без неё BLEND25/50/75 были тихим
    no-op. Вспышка выстрела соответствует оригиналу (вдвое тусклее) — множитель
    теперь из таблицы режимов, а не вручную в подкраске; ОТКЛОНЕНИЯ НЕТ
    (см. journal 2026-09-07: я ошибочно считал её отклонением).
    Регресс исключён побайтовым сравнением трасс против предыдущей версии.
  - **G4 СДЕЛАНА И ПРОВЕРЕНА ПОЛЬЗОВАТЕЛЕМ (2026-09-07):** `--backend=gl|sdl`
    (+ `DOOM2RPG_BACKEND`), заголовок окна показывает бэкенд, F12 снимает кадр в
    `capture-<бэкенд>-NNN.bmp` (960x640, без полос, снимается до вывода на экран —
    ровно тот кадр, что на экране; пользователь подтвердил идентичность).
    `--backend=sdl` пока честно откатывается на gl с сообщением.
  - **G5 СДЕЛАНА И ПОДТВЕРЖДЕНА (2026-09-07):** `new_src/render/sdl/` работает,
    `--backend=sdl` даёт полностью рабочий двумерный слой (меню, HUD, диалоги,
    шрифты, лут, комиксы, карта); вместо мира ровный 32,32,64 до G6.
    Совпадение с GL ДОКАЗАНО численно: меню отличается на 204 пикселя из 614400,
    все в одном пятне 13x9 = качающийся курсор, снятый в разные моменты.
    Память: SDL 59.1 МБ против GL 15.1 МБ (разворот 1 байт → 4).
    ВАЖНО на будущее: `SDL_SetTextureColorMod` ИГНОРИРУЕТСЯ `SDL_RenderGeometry`
    на metal — модуляция делается цветами вершин (замерено пробой).
  - **G6 СДЕЛАНА (2026-09-07):** трёхмерный вид на SDL работает. Проекция и
    отсечение на процессоре, порядок отрисовки идентичен GL. **Скорость: медиана
    63 FPS у ОБОИХ бэкендов** — резка и проекция на CPU в этой игре бесплатны.
    Предел ячеек тайлинга = 289 (17x17, СТРОГАЯ граница с учётом прокрутки лавы).
    Пользователь подтвердил: искажения есть, разбиением занимается G7.
  - **G7.1 СДЕЛАНА И ПРИНЯТА ПОЛЬЗОВАТЕЛЕМ (2026-09-07):** адаптивная тесселяция
    в `SdlScene3D` (сетка `n x n`, `n = ceil(sqrt(M/2px))`, `n <= 8`, без рекурсии)
    + отброс за-экранной геометрии. Пользователь: «сейчас хорошо выглядит».
    Числа: листья 38258 → 9193 (-76%), НА ЭКРАНЕ 6674 → 6674 без расхождений на
    16 тыс. кадров. Щелей нет по построению (проекция переводит прямые в прямые);
    проверено численно: отход новых вершин от кромки худшее 0.125 px канваса.
    Осталось по желанию: G7.2 (счётчики и время кадра) и G7.3 (режим замера со
    снятым ограничением частоты) — только измерения, картинку не меняют.
    ВАЖНО: 63 FPS ничего не измеряют (vsync + намеренная задержка 15 мс в
    `core/GameLoop.cpp:105-110`); честная цена требует времени кадра.
  - (историческая справка) Изначальная формулировка G7: РАЗБИЕНИЕ треугольников для
    уменьшения афинного искажения (ошибка падает квадратично с размером
    треугольника на экране). Делается ТОЛЬКО в SDL-реализации: в GL коррекция
    бесплатна железом, дробление там — чистые накладные расходы. Проектные
    вопросы: критерий мерить в ЭКРАННЫХ координатах (иначе дробим далёкие стены
    зря), а резать в ОБЪЕКТНЫХ (иначе UV и глубина проинтерполируются неверно);
    и порядок относительно уже существующей резки на повторения текстуры.
  - Знать при включении тумана: на SDL он гасит мир в ЧЁРНЫЙ, а не в цвет тумана
    (почти все мировые текстуры с ключом прозрачности, ADR-0022 запрещает дымку
    на таких). Ограничение шире формулировки ADR.
  - Снятие кадра: F12, а на маке **fn+F12** (иначе система перехватывает).
    Строка о снятом кадре идёт в стандартный вывод, не в поток ошибок.
  - **Отдельная задача, найдена попутно:** оригинал широко использует приглушённые
    режимы в интерфейсе (шрифт целиком BLEND50 через `setFontRenderMode(2)`,
    плашки софткеев и линии карты 50%, кнопки 25%, панели миниигр 75%, экранные
    кнопки — особая альфа). Мы их не запрашиваем нигде, кроме вспышки. Теперь,
    когда модуляция работает, можно сверять по одному элементу.

- **Режимы смешивания спрайтов (blend modes) — G1/G2 сделаны, ревью НЕ проходило.**
  Спека 2026-09-01-blend-modes.md, ADR-0019.
  - G1: до этого из 14 режимов работали ТОЛЬКО 3 (ADD) и 7 (SUB); режимы
    1/2/4/5/6/9/12 молча рисовались как обычные, а у шейдера мира вообще не было
    цветового члена. Теперь таблица режимов + uniform модуляции (`uColorMod`,
    аналог легасевого `glColor4f` при `GL_MODULATE`) + туман ПО РЕЖИМУ (раньше
    хардкод `!= 3 && != 7`, из-за чего у режимов 4/5/6/10 туман ошибочно оставался).
    На map00 визуально НИЧЕГО не меняет: тайлов 161/208/234/236/244 там нет.
  - G2: свечение торшера (тайл 193 SFX_LIGHTGLOW1 в ADD50) — принято пользователем.
  - **ОТКЛОНЕНИЯ ОТ ОРИГИНАЛА ПО РЕШЕНИЮ ПОЛЬЗОВАТЕЛЯ (2026-09-01), не баги:**
    1. Ореол торшера рисуется ПОВЕРХ тела лампы. В оригинале — ПЕРЕД телом
       (`src/Render.cpp:1646` рисует ореол и НЕ делает `return`, тело рисуется
       на `:1728`; буфера глубины нет, порядок рисования = порядок наложения).
       Проверено лично по исходнику, порт БЫЛ верен. Рецепт возврата — в
       комментарии у вызова в `new_src/render/World3D.cpp`.
    2. Огонь (тайл 130) рисуется ПОВЕРХ нагара (212) — смещение `d += 2` для 212
       в `computeDepth`. В ОРИГИНАЛЕ нагар тоже поверх огня: ключи сортировки у
       пары бит-в-бит равны (одна точка, один лист BSP, персональных смещений нет),
       а легасевый тай-брейк даёт возрастание индекса, и у нагара индекс больше
       (`src/Render.cpp:880-893`, цепочка `:2396-2397,2494`, обход `:1757-1759`).
       Нагар и огонь — ОДНИ И ТЕ ЖЕ спрайты 159/160/161 и 25/31/32, припаркованные
       в данных на (2,22) и телепортируемые катсценой (`LERPSPRITE` @3540-3583,
       триггер EVT 52 = проход на (7,19) лицом на запад; восстановление после
       сцены — INIT_MAP @708-723). Динамического нагара в проекте нет вовсе.
       Доказательства: journal 2026-09-01.
  - **ЗАКРЫТО 2026-09-01 (было долгом, принято пользователем):** тай-брейк
    сортировки спрайтов в листе приведён к легасевому. Была неустойчивая обменная
    сортировка — при равных ключах обмена нет, но обмены элементов с ДРУГИМИ
    ключами растаскивали равную пару (в листе 214 группа key=760 выходила как
    31,32,30, пара key=762 как 29,28). Стала устойчивая сортировка вставками; сбор
    идёт по возрастанию индекса, поэтому устойчивость сама даёт легасевое правило
    (`src/Render.cpp:880-893`, цепочка `:2396-2397,2494`, обход `:1757-1759`).
    После: 68 ничьих за прогон, ВСЕ по возрастанию, 0 обратных; спасённые из
    внутренних узлов при равенстве остаются позади своих, как `addNodeSprites`.
    Комментарий запрещает замену на `std::sort` (неустойчива -> вернёт дефект).
    Уточнение формулировки: сортировка была ДЕТЕРМИНИРОВАНА, но неустойчива —
    опасность не в мерцании кадр к кадру, а в том, что порядок равной пары зависел
    от расположения ПОСТОРОННИХ элементов (появился/погас соседний спрайт, сдвинулась
    камера -> перевернулась несвязанная пара). Отклонение №2 от этой правки не
    зависит: `+2` делает ключи неравными, спрайты 25/159/160/161 в ничьих не участвуют.
  - G3 НЕ сделана и нужна: мы НАЗНАЧАЕМ режимы девяти тайлам, но до G1 применялись
    только два → часть спрайтов могла рисоваться неверно. Плюс `drawPoly` всегда
    зовёт `applyBatchState(0)`, тогда как легаси выбирает режим по текстуре
    (тайл 161 → BLEND50, `src/Render.cpp:950-963`), и отсутствует runtime-override
    тайла 212 → SUB (`src/GLES.cpp:609-611`).
  - Тайл 240 (WATER_STREAM) — следующий потребитель, НЕ кадровая анимация:
    четырёхугольник, натянутый между двумя точками, со скроллом текстуры
    (`renderStreamSprite`, `src/Render.cpp:1418-1495`), ключ сортировки форсирован
    в `0x80000000` (рисуется последним). Пользователь просил разобраться после
    режимов.
- **Отладочный оверлей координат игрока (НАШ, не порт):** строка
  `tile x,y  xy X,Y  dir d  ang n` в левом нижнем углу 3D-вида,
  `new_src/ui/Hud.cpp` `drawDebugPosition`, вызов из `GameContext::render`.
  Переключение — клавиша **B** (SDL_SCANCODE, была зарезервирована-незамаплена;
  в легаси это сброс бота, которого у нас нет). По умолчанию ВКЛЮЧЕН.
  Побочно выяснено: `Hud::draw` не имеет вызывающего вовсе — рендер дёргает её
  части напрямую (это и есть причина пункта PLAN.md про невидимые виджеты HUD).

- **Ветка оружия — G1/G3 сделаны, ревью PASS, AWAITING USER EYES.**
  Спека 2026-08-31-weapon-branch.md, ADR-0017/0018.
  1. **Бензопила наносит урон** (была отвергнута guard'ом как «оружие со снарядом»).
     Бить В УПОР, ровно с 1 клетки: ~20-27 за удар (винтовка 8-10), убивает за 2-3
     удара, замах ~1 с. С 2 клеток урона нет, но ход тратится (RANGEMAX 1 — так в
     оригинале). Зомби ×2, saw goblin ×0.125. Промах пилой больше НЕ будит монстра.
  2. **Мебель** (тайлы 121 стол / 135 стул) рубится ТОЛЬКО пилой: исчезает, клетка
     проходима, стр. 89 + 5 XP. Другим оружием даже не выбирается целью.
  3. **Ящики** (152) не разрушаются ничем (маска 0), открытие по «use» как раньше.
  4. **Баррикада** (178) ломается любым оружием кроме святой воды: «сломанный»
     кадр, перестаёт блокировать, сущность выживает. Унитаз/раковина (123/127)
     дают стр. 89 + XP, но остаются (фонтан воды отложен).
  5. **Регресс-проверка**: выстрел из ВИНТОВКИ в дверь дважды — второй и далее
     должны идти как промах (был баг: бесконечные попадания). Убить монстра пилой
     и винтовкой — боль/смерть/труп/XP ровно как раньше.
  6. **Унитаз/раковина → фонтан воды** (принято пользователем 2026-08-31):
     удар ЛЮБЫМ оружием кроме святой воды (id 2) заменяет спрайт фонтаном НА
     МЕСТЕ, объект остаётся видимым, клетка становится проходимой, сообщения и
     XP НЕ даются (легаси подменяет def до `died`). Фонтан АНИМИРОВАН: своя
     ветка рендера, 2 кадра по 128 мс, глобальная фаза — все фонтаны в такт.
     Отложено: «поднять и метнуть» (INTERACT_PICKUP path B — нужен WP_ITEM и
     снаряды projType 13) и пополнение святой воды по действию.
  НЕ сделано: G2 (рост силы 10→12 за 30 ударов, тряска), G4 (гибы трупов),
  G5 (настоящие снаряды — святая вода, плазма, ракеты, BFG, куб душ, брошенный
  предмет; 6 видов оружия по-прежнему отказываются стрелять, нужен пул динамических
  спрайтов).

- **Полки / блокировка / ящики (G1-G4) — ЗАКРЫТО. Все четыре ревью PASS,
  ПОЛНОСТЬЮ ПРИНЯТО ПОЛЬЗОВАТЕЛЕМ на экране 2026-08-30: подбор с полок,
  подбор с пола, блокировки, открытие ящиков с содержимым.** Спека 2026-08-30-blocking-crates-shelf-pickup.md, ADR-0015/0016.
  1. **Полки**: `EV_GIVEITEM` режим 0 (была заглушка). Встать ЛИЦОМ к тайлу, не
     наступая, нажать действие. map00: (12,17), (13,15), (8,24) — два предмета,
     (25,17). Ход ТРАТИТСЯ. В логе не должно остаться `sprite-touch give unsupported`.
  2. **Блокировка**: сущность на каждый спрайт с def (191 на map00, 193/275 слотов).
     Упереться: ящики (6,22) (7,18) (11,13) (14,8) (15,23) (15,26) (18,3); торшеры
     (4,18) (4,20); унитазы/раковина (12,10) (12,11) (14,11); решётчатые окна
     (18,24) (18,26) — без def. Сдвиг привязки: (2,21) (18,24) (11,25) (12,18) должны
     блокировать клетку У СТЕНЫ, клетка перед ними проходима. Невидимая стена посреди
     пустого коридора = промах сдвига, нужны координаты.
  3. **Ящики**: до открытия не пускают; действие с соседней клетки → 4 кадра ~0,6 с,
     остаётся открытым (НЕ исчезает и НЕ схлопывается как дверь); клетка проходима
     СРАЗУ после нажатия, ещё в анимации; повторное действие ничего не делает;
     выстрел не открывает и не разрушает.
  4. **Содержимое**: диалог с заголовком `Crate Con-tents:` (общее «You Got:» =
     источник не опознан). Монстры ходят РОВНО один раз, после закрытия окна.
     (7,18) открывается только с запада, (15,23) — с запада и юга (так в скриптах).
  Не портировано: звук, ET_MONSTERBLOCK_ITEM, dropped-плумбинг, help-попапы,
  ET_ENV_DAMAGE, разрушение ящиков взрывом.
- **Подбор мировых предметов — реализован, оба ревью PASS, AWAITING USER EYES.**
  `domain/game/ItemPickup` + `Game::touchTile`; спавн ET_ITEM в `Game::loadEntities`
  (25 предметов на map00); подбор в `PlayerActions.cpp:62` по занятости целевого
  тайла, ход не тратится; `Player::give` доведён до легаси (авто-экипировка,
  quiet, sentry-bot, holy water). Спека 2026-08-29-world-item-pickup.md, ADR-0014.
  Проверить на экране: кредит (2,15) → «You got N UAC Credits» N∈2..4 и монета
  исчезает; аптечка → str 85; патроны → N∈2..5; предметы проходимы насквозь;
  подобранное не возрождается после отхода; число ходов монстров после шага с
  подбором = как при обычном шаге; подбор оружия ПЕРЕКЛЮЧАЕТ оружие в руках.
  Приёмку sentry-бота на map00 провести НЕЛЬЗЯ — на карте его нет (спрайт 65 =
  дверь, тайл 271); форс-кадра оружия на map00 — no-op (кадр 2 уже в данных).
  Не портировано: звук 1054, ET_MONSTERBLOCK_ITEM, dropped-плумбинг, help-попапы.
- **User acceptance batch (map00 intro area)** — implemented + reviewed, AWAITING
  USER EYES on the fresh build:
  1. Lift cutscene end scene: squad imps 185-188/grenade hidden via NOENTITY
     hidden-bit path; corpse prop 105 lies at (5,19) + standing NPC beside it
     (= faithful); imp corpse at (3,26). If "legs" persist → capture stderr.
  2. Loot dwell: E on corpse → 500ms crouch → red "Looted Items:" top panel,
     E pages/closes, TAB/BACK closes, grant on close, stand-up, turn consumed.
     Spec docs/architecture/specs/2026-08-25-loot-dwell-ui.md, ADR-0006;
     reviewer PASS (see journal 2026-08-25 review entry).
  3. Blue door sp22: opens with card. "Doesn't let through" — root cause NOT yet
     proven; self-disable replay refuted (eventMatches honors bit19). Need one
     stderr run: `[dbg] moveBlocked … by spr=N type=T` names the blocker;
     `[script] DOOROP sprite=22` close pair would name a script.
- **Deferred by user:** hangar door unlock chain (item dropped this cycle).
- Fire additive flicker + scorch stains: done, user-confirmed matches original.
- **Known bugs (deferred):**
  - **G7 спеки 2026-09-11-sokol-gfx-backend.md — окружение D3D11 для sokol, НЕ
    СДЕЛАНО, отложено пользователем 2026-09-22.** Причина: на этой машине (macOS)
    DirectX нельзя ни собрать, ни проверить — делать только на Windows. Что нужно
    (всё по спеке §G7): устройство и swapchain D3D11 создаём САМИ (SDL2 их не даёт,
    `sokol_gfx` тоже — «does not create a window, swapchain or the 3D-API
    context/device»); swapchain в этой ревизии sokol передаётся на каждый кадр
    (`sg_swapchain` в `sg_pass`, как на Metal); буфер глубины не нужен
    (`SG_PIXELFORMAT_NONE`); `depth_fix = (0.5, 0.5)` как на Metal (D3D11 отсекает
    `0<=z<=w`); вычитающий режим 7 — альфа-множители `(ZERO, ONE_MINUS_SRC_ALPHA)`,
    потому что D3D11 отвергает цветовые множители в альфа-слоте; F12 на D3D11
    недоступен (ADR-0026, у sokol нет чтения обратно). Сейчас выбор
    `-DDOOM2RPG_SOKOL_BACKEND=d3d11` упирается в заглушку в
    `new_src/render/sokol/SgEnvironment.cpp` («not implemented yet; configure with
    -DDOOM2RPG_SOKOL_BACKEND=glcore»), то есть падает внятно, а не чёрным экраном.
    Ограничение `sokol-shdc`: Windows ARM64 и 32-битная Windows не поддерживаются.
  - Camera judder in cinematics (unchanged).
  - Subtitles: CAMERA_STR bit14 showCinPlayer not portable yet; portrait art
    fallback for style 8.
  - **Расхождения с оригиналом, найденные ревьюерами 2026-08-30 (низкий приоритет,
    подробности в journal.md за 2026-08-30):**
    1. У монстров не стираются биты ориентации (`src/Game.cpp:438`,
       `mapSpriteInfo &= 0xF0FFFFFF`) — на map00 не проявляется (ни один монстр
       не попал под nudge), но ориентированный спрайт монстра будет трассирован
       как отрезок стены ±32 вместо круга r=25. Одна строка в ветке монстра
       `Game::loadEntities`.
    2. `ET_DOOR` получает `kInfoActive` (`Game.cpp` ветка двери), которого в
       легаси нет (`src/Entity.cpp:50-112` — дверной ветки не существует) →
       `Combat::calcHitEntity` считает выстрел в дверь попаданием, в оригинале
       промах. НАБЛЮДАЕМО.
    3. ~~Пре-пасс не портирует инъекции анимации 156/236~~ — **ЗАКРЫТО
       2026-08-31** вместе с анимацией фонтана (см. journal 2026-08-31).
       Проверить визуально нельзя: тайлов 156/236 на map00 нет.
       ОСТАЛОСЬ из того же класса: персональная ветка рендера тайла 240
       (WATER_STREAM) — это не кадровая анимация, а луч в видовом пространстве
       (`src/Render.cpp:1418-1495 renderStreamSprite`), отдельная фича; и
       свечение торшера 136 (нужен режим ADD50 с модуляцией цвета, которого нет
       в `applyBatchState`). Мигание, приписываемое торшеру, — мёртвый код:
       при одном кадре выражение всегда даёт 0.
    4. Цепочка действия (`PlayerActions.cpp`, ветка Use) не содержит легасевых
       `state == ST_PLAYING` и `snapMonsters(true)` перед advanceTurn
       (`src/PlayingInputHandler.cpp:399-402`). Сегодня безвредно — все опкоды,
       меняющие состояние, ставят `skipAdvanceTurn`; мина для будущих опкодов.
    5. Дверная ветка сканирует цепочку сущностей тайла (`DoorSystem::useDoorFacing`)
       вместо легасевой проверки ИЗБРАННОЙ цели по типу+дистанции
       (`src/PlayingInputHandler.cpp:445`). Хойст elect в G3 убрал единственное
       препятствие для приведения к оригиналу. Рефакторинг, без видимого эффекта.
- Backlog: monsters/AI/combat; automap; save/load; EV_CHANGE_MAP real
  transitions; renderMode blending leftovers; stderr log polish.
- **ВАЖНО к будущей работе «атаки монстров»** (2026-08-31): монстры сейчас НЕ
  атакуют и не двигаются (`MonsterSystem.cpp:139`), игрок неуязвим — в боевую
  систему ведут только `Player::fireWeapon` и скриптовый `performAttack`.
  Когда атаки монстров будут реализованы, НЕЛЬЗЯ пропускать их через guard
  `proj > 0` из `Player::fireWeapon`: у BITE/CLAW/PUNCH/CHARGE (weapon id 15-18)
  `projType == 1`, и в оригинале они МГНОВЕННЫЕ (падают в `default:`), т.е. guard
  заблокировал бы самую частую атаку монстров. Для оружия ИГРОКА тот же guard
  точен: ошибочно не отвергает ничего (6 отвергнутых видов действительно требуют
  пула снарядов). Доказательство: `docs/research/2026-08-31-projtype-inventory.md`,
  `docs/original-code/combat.md` §10. Там же: projType 1 у монстров требует не
  только урона, но и спрайта удара (anim 245/246/247 через `gsprite_allocAnim`).

## Fidelity pass 2026-08-23 (doors + sprites)

Implemented A1–A8 + B1–B5 from the spec above across `Game.{h,cpp}`,
`World3D.{h,cpp}`, `Main.cpp`; reviewer PASS. Key behavior now matching legacy:
slip-door vertical split, slide-door UV pinning, DOORLERP lifetime, solidity
timeline, faced-door trigger (Chebyshev ≤ 1 tile), open-frame texture,
tile-granular auto-close occupancy, camera pull-back nudge, portal-eye z range,
billboard UV flips, FLAT plane branch, height-snapped sort keys with bias chain.
Follow-up fixes same day: media REFERENCE record resolution (vanishing green
doors) and faithful player collision trace (walls solid; flag semantics 0–7).
All user-verified.
Still missing (unchanged): keycard unlock path was delivered by Phase 5 scripts;
monster-blocks-close, door sounds, water streams remain open.

## Feature audit vs PLAN.md (2026-08-22)

Full module/wiring details: [architecture/README.md](architecture/README.md).

### Phases 1–2.7 — essentially complete

| Item | Status | Evidence |
|---|---|---|
| AppContext + .ipa (zlib), SDL2 window, GL 3.3 | done | core/AppContext.cpp:24-51, platform/Window.cpp:34-82, io/ZipArchive.cpp:128-141 |
| RenderBackend batch+shaders, letterbox 480x320 | done | render/RenderBackend.cpp:16-57, Window.h:25-26 |
| Font.bmp, Graphics2D, full HUD set | done | text/Font.cpp:7-97, render/Graphics2D.cpp, ui/Hud.cpp (**demo-fed values**, Hud.h:112-118) |
| newMappings/newPalettes/newTexels → MediaLoader | done | io/Media.cpp:10-153 (byte-exact fix verified, see PLAN.md) |
| MapParser full mapXX.bin + decodePolys (1787 polys map00) | done | domain/world/MapParser.cpp:13-298 |

### Phase 3 — one sub-item open

| Item | Status | Evidence / gap |
|---|---|---|
| Camera3D 14.14 fixed → float MVP | done | render/Camera3D.cpp:15-103 |
| World3D shader/fan/indexed textures/sky/fog/sprites/doors | done | render/World3D.cpp (see architecture README) |
| BSP traversal | **partial** | `cullBoundingBox` NOT ported — walks every node (World3D.cpp:789-793). PLAN's "simplified 2D-reject" claim does not match code. |
| drawNodeGeometry faceCull/swapXY/expandEdgePoly | **open** | edge expansion lives in MapParser (:206-234); GL path draws as-is; poly-flag culling unported |
| Texture animation (lava UV, AUTO_ANIMATE) | partial | World3D.cpp:363-369, :520-522; fire sprites lack the bit |
| Camera controls | done | Main.cpp:260-273; movement is now *discrete* legacy-style (90° turns, 1-tile steps), not continuous strafe as PLAN describes |

### Phase 4 — mixed

| Item | Status | Evidence / gap |
|---|---|---|
| Enums.h, CombatEntity, Entity, Player, Game entityDb/link/unlink | done | domain/game/* (Player classes: only marine defaults, Player.h:69) |
| Doors: load/open/close/E-key/auto-close | done | Game.cpp:43-74, :78-96, :130-245; locked doors refuse w/o key check (:100) |
| Player collisions (trace) | **partial — ahead of plan** | CapsuleToLineTrace + canPlayerStep exist in simplified form (Game.cpp:263-337); no CONTENTS-masked entityDb trace. PLAN checkbox should flip. |
| Discrete cell movement | **partial — ahead of plan** | Player.cpp:13-72 wired ad-hoc in Main.cpp:333-384, not inside a Game-driven advanceTurn. PLAN checkbox should flip. |
| loadEntities from TILE sprites | partial | creates **door entities only** (tile range filter Game.cpp:61); items/monsters/NPC/decor never instantiated |
| Monsters (EntityMonster/AI/activation) | **missing** | fwd-decl only (Entity.h:12); enums pre-staged (Enums.h:75-106) |
| Item pickup (touchedItem/give) | **missing** | Player::give exists (Player.cpp:95) but zero call sites from world pickup |
| Weapons/combat calcHit/calcDamage | **missing** | 0 hits; data ready but unused: io/Tables.cpp, io/EntityDefs.cpp |

### Phase 5 — missing

STALE as of 2026-08-26: the state machine, tileEvents VM, dialogs and combat all exist now
(`core/GameContext.cpp`, `domain/game/ScriptVM.cpp`, `domain/game/DialogSystem.cpp`,
`domain/game/Combat.cpp`), and `GameLoop` is wired (`core/AppContext.cpp:62`), not a stub.
tileEvents/bytecode parsed and stored but not interpreted (domain/world/MapData.h:73-75).

## Biggest gaps (priority hints)

1. Phase 5 skeleton: real game-state machine replacing the Main.cpp monolith.
2. Monsters: EntityMonster class + AI + activation + rendering.
3. Combat/weapons: consume tables.bin/entities.bin data (calcHit/calcDamage).
4. Item pickup: touchedItem → Player::give; extend loadEntities beyond doors.
5. BSP cullBoundingBox port (or documented decision to skip).
6. PLAN checkboxes lag the code: collisions & discrete movement are partially implemented.

## Next steps

- Decide next unit of work (suggest: PLAN checkbox sync + Phase 4 monsters or game-state skeleton).

## 2026-08-26 — playtest fixes in progress (see docs/journal.md for the full entry)

Camera key-0 CONFIRMED good by the user. Four playtest defects; research done for all
four (4 researchers in parallel), curated into `docs/original-code/{combat,rendering,cutscenes-camera}.md`
with raw logs in `docs/research/2026-08-26-*.md`.

| # | Defect | Root cause | Status |
|---|---|---|---|
| 4 | Shaft lift rides rushed | tween indices are camera-local; we still added the legacy global rebase, so every delta silently resolved to 0 | **DONE** — reviewed PASS, user-confirmed "работает идеально" |
| 2 | Health bar only on the adjacent tile | original uses a 6-tile facing ray; monsters are never distance-gated. Our spec said "one tile" | **DONE** — user-confirmed |
| 3 | Standing on a corpse blocked the shot | own-tile entity sorts first (frac −1); the original's corpse branch accepts only at exactly one tile and never breaks | **DONE** — user-confirmed |
| 1 | Rifle too high and too small | `draw2DSprite` is a world-space billboard 400 units ahead, magnified by the world projection (Kx 1.336 / Ky 1.340) — not a 1:1 screen blit | **DONE** — user-confirmed |
| 5 | Cutscene viewport slid down (found in round 2) | the original never changes the viewport for cinematics; `cinRect` is the cockpit-overlay anchor, and `BeginFrame` discards the y | **DONE** — user-confirmed |
| 6 | Wall shots fired and burned the turn (regression from our 6-tile ray) | world hit has `def == nullptr`, so the wall-push branch was dead code | **DONE** — user-confirmed |

Spec: `docs/architecture/specs/2026-08-26-combat-stage1-fixes.md` (6 groups),
ADR `docs/architecture/adr/0009-legacy-world-viewport.md` (user chose to restore the legacy
world band (1,7,478,248), horizon back at y=131). `2026-08-26-combat-stage1.md` is partially
superseded. G4 (bottom HUD panel 480x64 at y=256) landed as a prerequisite for G5.

Regression list confirmed clean by the user: doors, loot, dialogs, TAB "Turn Passed",
monster wake/pain/death/corpse looting.

## Next steps

All four playtest defects + the self-inflicted wall-shot regression are user-confirmed fixed
(2026-08-26): lift ride cutscenes, health bar at range, shooting from a corpse tile, rifle
size/height, cutscene entry without a vertical step, wall shot not consuming ammo/turn,
impact point and flash correct.

Also user-confirmed on 2026-08-26: cinematic fov/weapon-suppression fixes (weapon unchanged in
gameplay, absent in cinematics) and the cinematic letterbox black bars.

## Current focus — MENU COMPLETE (2026-08-29, every group user-confirmed)

8 of 8 groups, spec `docs/architecture/specs/2026-08-28-menu.md` + ADR 0013:
G1 `io/MenuData` (menus.bin parsed, golden-checked at boot), G2 menu primitives
and sheets, G3 the menu opens (root list, wobbling cursor, soft keys, health/shield
readout), G4 scrolling + the 4-sheet scrollbar + faithful drag on list and bar,
G5 the navigation stack and sub-screens, G7 confirm screens, G6 help pages and the
PDA shell, G8 info buttons and the torn-page popup. Inventory and weapons bodies
are built in code as the original does, and selecting a weapon row equips it — the
rewrite's first weapon switch from the UI.

Working: root list, Inventory -> Weapons (with the equip), Status -> Player values,
Game Help's ten topics as real text, PDA shell, Options, the four confirms, per-row
info popups.

Deliberately refusing, with reasons logged: Save Game, View Map, Restart Level and
Save & Quit YES (no save system, no automap state, no map reload), Credits,
Controls, Nano Drinks, item use, the details screen. They draw as NORMAL rows —
the legacy `ITEM_DISABLED` look belongs only to rows the DATA disables, a
distinction the reference build taught us after we got it wrong.

Deviations, all recorded at the code and in the spec's DEVIATION section:
soft-key hit rects narrowed to the arrow icons; `kViewPx` 241 chosen over the
port-derived 256 after the user saw the last row jammed against the border; and
the help-page scroll clamp bounded by content height rather than the port's
item-count bound (neither side is J2ME behaviour — both are `[GEC]`).

Known gaps: no key bound to the info action (popup is mouse-only), `%NN` help
arguments would survive literally, quest/journal content absent, Level/Grades
values unsourced.

## Previous focus — UI layer COMPLETE (2026-08-28, every group user-confirmed)

Custom immediate-mode UI, 8 groups of 8, spec
`docs/architecture/specs/2026-08-27-ui-layer.md` + ADR 0012:
G1 real canvas scissor + window->canvas cursor mapping; G2 primitives, `UiState`,
`UiAssets`; G3 `core/UiInputCollector` (mouse did not exist before); G4 bottom HUD
via `ui.*` with clickable soft keys; G7 dialog box with clickable page icons;
G5 loot list; G6 view weapon.

**The layer invariant is now checkable, not merely stated**:
`grep -rn "#include \"domain/game\|#include \"core/" new_src/ui/` is empty.
UI receives values (per-frame model structs, never setter-fed fields) and returns
intents (`UiAction`), which `GameContext::applyUiAction` maps into the SAME
`pendingActions_` queue the keyboard uses — so mouse and keys cannot diverge on
turn semantics.

Deliberate deviations, all documented at the code and in the spec:
- soft-key hit rects narrowed to the arrow icons (9,268,32,32)/(438,268,32,32) at
  the user's request, so the `Menu`/`Map`/`Wait` text is not clickable — strict
  subsets of the legacy (0,256,52,64)/(428,256,52,64);
- pass-turn moved to the portrait (219,264,42,36), which is where the original has
  it (`src/Hud.cpp:76,1343`); the `Wait` literal has no button in the original and
  ours was invented by G4;
- D1: `ViewWeapon` keeps hand clipping (scissor cannot trim a source sub-rect);
- D2: the `flashDone` latch left the draw path, so the flash shows one rendered
  frame instead of two (~15 ms shorter, never later). User-confirmed acceptable.

Facts gathered on the way (in `docs/original-code/ui.md`): the arrow art IS the
soft key's pressed state, not a separate widget; the portrait is PASSTURN, the
weapon icon NEXTWEAPON, shield/health/keys are drinks/items/questlog; the loot
list's only touch target is the whole screen; dialog page icons are buttons 5/6/7/8
(90x90 at (390,20)/(390,110)) drawing at 75% alpha until pressed; the menu uses a
different scrollbar art from the dialog/loot one, selection is a wobbling cursor
glyph rather than a highlight bar, and there is no key repeat.

## Previous focus — decomposition COMPLETE (2026-08-27, user-confirmed)

Phase 1: 7/7, Phase 2: 6/6. `GameContext.cpp` 1626 -> 535, `Game.cpp` 1565 -> 305.
Thirteen modules, pointwise `Env` injection, no singleton or context back-references.
All five duplicated entityDb helper copies collapsed into `EntityDb` (P2-GF).

Phase 3 also COMPLETE (2026-08-27, each group user-confirmed):
- B2: `TraceHit` is the only carrier of trace results; the hand-decode of `def == nullptr` that
  produced the dead wall-push branch is gone (acceptance grep empty).
- C2: weapon tables are parsed structs (`WeaponDef`/`WeaponPose`) instead of a byte vector read
  through field-index arithmetic; identity proven against the shipped `tables.bin` for all rows.
- C1/C3: content masks (`Contents::*`) and tile/sprite encodings (`MapBits.h`) are composed from
  named bits and pinned; `13997`/`21741`/`13501`/`0xF000000`/`0x3000000` appear only on asserts.
- C4: `Combat::tileDistSq(tiles)` removes the off-by-one of `tileDistances[n]`.
- F sweep: all 15 forwarders gone from `Game.h` (115 lines, was 236). `setXPSystems` stays as a
  deliberate composite of two peer wirings.

Sizes: `GameContext.cpp` 1626 -> 535, `Game.cpp` 1565 -> 288, `Game.h` 236 -> 115.

Entity `info` bits: `0x20000000` (breathing suppressed, inverted setter) and `0x400000`
(dirty/needs-save) are CONFIRMED and named; `0x200000` and `0x4000000` are write-only in the
original with no reader anywhere and stay unnamed on purpose (our "highlight marker" comment
was refuted). The entity word does NOT share `mapSpriteInfo`'s layout, and a third colliding
bitfield exists (the `getSaveHandle` result) — see `docs/original-code/entities.md` §5.

### Original plan (kept for reference)

## Current focus — decomposition

Spec `docs/architecture/specs/2026-08-26-decomposition.md` (19 groups, 2 phases),
ADR 0010 (modules + Env injection), ADR 0011 (typed TraceHit + named encodings).
Zero behaviour change; acceptance = the user sees no difference. Phase 1 (GameContext ->
CinematicCamera/LootSession/Targeting/ViewWeapon/SceneRenderer/PlayerActions) is sequential;
Phase 2 (Game -> TraceSystem/DoorSystem/MonsterSystem/SpriteLerps/CorpseLoot/EntityDb behind
forwarders) runs in parallel with it.

### Decomposition debt — MUST be paid in P2-GF (do not lose this)

`DoorSystem` carries verbatim COPIES of `Game::findMapEntity`, `linkEntity` and
`unlinkEntity` (private members, marked "do not add logic here"), because those helpers
belong to `EntityDb` which only exists from P2-GF, and a module `Env` may not hold a `Game*`.
P2-GC (`MonsterSystem`) needs the same helpers (`corpsifyMonster` relinks entities) and must
copy the SAME comment rather than invent a variant. **P2-GF deletes both copies and switches
them to `EntityDb*`.** Duplicated logic drifting apart is exactly what this refactor exists to
prevent, so this is the one debt that cannot be quietly deferred.

`MonsterSystem::Env` carries a `std::function<void(int)> snapLerps` shim because the lerp pool
belongs to P2-GD; **P2-GD must replace it with a `SpriteLerps*`** and repoint `Env::clockMs` at
the lerp clock's new owner. `Game::snapSpriteLerps` (= legacy `snapLerpSprites`,
`src/Game.cpp:1149-1166`) becomes `SpriteLerps::snap`.

Also transitional, tagged FORWARDER, scheduled to die: `Game::lastTraceHits()` rebuilds a
legacy vector from `TraceSystem::hits()` per call (dies in P3-B2); `Game::traceMove` keeps an
unused `const MapData&` parameter; `Game::performDoorEvent` / `useDoorFacing` /
`GameContext::getHeight` are one-line forwarders.

### Known gaps found in passing, not yet scheduled

- `Hud::clearMessages()` has zero call sites; legacy zeroes `msgCount` on ST_CAMERA entry
  (`src/Canvas.cpp:1209`) and never draws messages during a cinematic.
- Bottom HUD panel draws background only (widgets live in the uncalled `Hud::draw`).
- A far `ET_SPRITEWALL`/`ET_DOOR`/`ET_DECOR_NOCLIP` election becomes an air shot here, while
  legacy `src/PlayingInputHandler.cpp:509` fires at the entity for any `eType != 0`.
- Then: bottom-panel widgets (shield/health/portrait/weapon icon/keys currently unreachable —
  they live in `Hud::draw`, which has no caller in the gameplay render path).
- Cleanup: drop the TEMP `[cam] nextKey` print (rides now eye-confirmed), throttle the tween
  out-of-range diagnostic, fix the stale comment placement in `MayaCamera.cpp`.
