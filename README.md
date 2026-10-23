# CP2077 Import для 3ds Max

Плагин для 3ds Max: импорт персонажей и оружия из **Cyberpunk 2077** со скелетом, скином, текстурами и оружием в руке, затем загрузка игровых анимаций на этот скелет.

Файлы игры извлекает **WolvenKit.CLI**. Плагин запускает его сам, извлечённое складывает в кэш и при повторном импорте берёт оттуда.

- персонаж целиком по его `.ent` и внешности (тело, голова, волосы, одежда, броня);
- все риги персонажа сливаются в один скелет, меши на нём со Skin;
- материалы с текстурами, многослойные материалы игры запекаются в одну цветную текстуру;
- оружие как отдельный скелет на слоте руки;
- выбор LOD;
- анимации: по одному клипу или пачкой подряд на одну шкалу, список загруженных клипов (проиграть / удалить), имена клипов в note track и секциях **WhiteoutDex** (для экспорта в MDX).

---

## Установка (готовая сборка)

1. Скачайте `CP2077Import-Max2027.zip` из [Releases](../../releases) и распакуйте в любую папку.
2. Закройте 3ds Max и запустите **`INSTALL.bat`**. Плагин ляжет в `%APPDATA%\Autodesk\ApplicationPlugins\CP2077Import` (права администратора не нужны).
3. Запустите Max: в главном меню появится **CP2077** с пунктами *Import Character / Weapon* и *Animations*.

Готовая сборка — для **3ds Max 2027**. Для других версий (2022–2026) соберите из исходников, см. ниже.

### Что ещё нужно

| | |
|---|---|
| Cyberpunk 2077 | папка игры (внутри `archive\pc\content`; `ep1` подхватывается сам) |
| WolvenKit.CLI | архив `WolvenKit.Console-<версия>.zip` из [релизов WolvenKit](https://github.com/WolvenKit/WolvenKit/releases), проверено на 8.19 и 9.1. Нужен файл `WolvenKit.CLI.exe` |

### Первый запуск

**CP2077 → Import Character / Weapon**:

1. **Game folder** — папка игры, **WolvenKit CLI** — путь к `WolvenKit.CLI.exe`, затем **Save settings**.
2. **Build index** — один раз составить список всех `.ent` и `.anims` игры (от минуты до нескольких). Пересобирать только после патча игры.

---

## Шаг 1 — персонаж и оружие

1. **Character → Search**: слова через пробел (`judy`, `gang maelstrom`, `adam_smasher`). Без индекса можно вписать полный путь к `.ent`.
2. Выберите сущность и её внешность (**Appearance**).
3. По желанию: **Attach a weapon**, найдите оружие (`w_` + название), выберите внешность. **Slot** по умолчанию `WeaponRight`.
4. **Options**:
   - **LOD** — 1 полный, 2, 3… легче, если у меша такие есть (иначе берётся ближайший; в отчёте написано, какие LOD нашлись);
   - **Textures** — материалы с текстурами;
   - **Flip normal green** — если рельеф выглядит вдавленным;
   - **Decals** — наклейки (логотипы, надписи);
   - **Physical / Standard Material** — тип материалов.
5. **Import**. Отчёт внизу окна; подробности в `import.log`.

В сцене:

```
npc_xxx            Point-хелпер скелета
  Root
    Hips ...       кости (Bone objects)
      WeaponRight
        w_xxx      хелпер оружия на слоте
t0_body, hair ...  меши со Skin, Multi/Sub-материалы с текстурами
```

Единицы: 1 м игры = 1 м в Max (по системной единице Max).

## Шаг 2 — анимации

**CP2077 → Animations**:

1. **Skeleton** — какой импортированный скелет анимировать.
2. **Animset** — сначала animsets самого персонажа; **Search all animsets** ищет по индексу (`walk`, `locomotion`, `pistol`). *Use a local .anims.glb* — готовый файл.
3. **List clips** (или двойной клик по animset).
4. Загрузка:
   - **Add selected clip** / двойной клик по клипу — клип добавляется после уже загруженных, через **Gap** кадров. Самый первый клип начинается с кадра ползунка; до него держится поза покоя.
   - **Add all (filtered)** — все клипы (или подходящие под **Filter**) подряд после загруженных.
   - **Back to rest pose** — снять всю анимацию.
5. **Loaded clips** — загруженные клипы с кадрами: **Play selected** (или двойной клик) проигрывает только этот клип, **Delete selected** удаляет его ключи (остальные остаются на месте), **Back to my timeline** возвращает ваш диапазон шкалы. Свой диапазон шкалы плагин не обрезает.

Кости сопоставляются по именам. Кости оружия, импортированного вместе с персонажем, анимируются тем же клипом (у боссов анимация оружия лежит в animset персонажа).

### Имена клипов и WhiteoutDex

Каждый клип записывается в note track корня сцены в формате WhiteoutDex/NeoDex: два ключа `"Имя"` на начале и конце. Если установлен [WhiteoutDex](https://github.com/FernandoS27/WhiteoutDex), плагин сразу переносит клипы в его **Sequence Manager**. Секции, которые вы сдвинули или переименовали в WhiteoutDex, плагин читает обратно; их настройки (NonLooping, Rarity, MoveSpeed) сохраняются. Копия имён есть и на note track хелпера скелета (видно в Dope Sheet).

## Импорт из проекта WolvenKit

Если файлы уже извлечены в WolvenKit (GUI): **Import a WolvenKit project file (.ent.json)** или *File → Import → «Cyberpunk 2077 entity (WolvenKit)»*.

---

## Как это работает

```
 3ds Max                           CP2077Import.dlu                          WolvenKit.CLI
 ───────                           ────────────────                          ─────────────
 окно (MAXScript) ──► CP77Cpp ──►  1. нужные файлы есть в кэше? ── нет ──►   uncook / convert serialize
                                                                              (.ent .app .rig → JSON,
                                                                               .mesh → GLB + Material.json
                                                                               + текстуры png, .anims → GLB)
                                   2. сборка (C++ ядро, без SDK)
                                   3. сцена Max
```

**Сборка сущности** идёт по тем же связям, что и в игре:

```
.ent  appearances[имя]  ──►  .app + appearanceName
.app  внешность         ──►  компоненты, partsValues (ещё .ent), partsOverrides (meshAppearance / chunkMask)
entAnimatedComponent    ──►  .rig (+ animsets для шага 2)
entSkinnedMeshComponent ──►  .mesh, skinning.bindName ──► его скелет
entMeshComponent        ──►  .mesh, parentTransform ──► кость / слот
entSlotComponent        ──►  WeaponRight → кость + смещение
```

- **Скелет.** Все риги персонажа сливаются по именам костей. Порядок — как в аддоне WolvenKit для Blender: риг деформаций идёт после рига, к которому он привязан (`controlBinding`). Поза покоя — A-поза рига.
- **Скин** строится в позе покоя скелета (как экспорт WolvenKit «WithRig»); веса — из GLB меша.
- **Материалы** читаются из `<mesh>.Material.json` по внешности меша. Обычные текстуры берутся как есть. Многослойные материалы (`multilayered.mt`) запекаются: цвет каждого слоя из его `.mltemplate`, слои накладываются по маскам из `.mlmask` → `cache\_cp77_textures`. Карты нормалей игры хранят только X/Y — синий канал восстанавливается; в Max они читаются как raw-данные (без цветокоррекции).
- **Анимации**: WolvenKit пишет animset в GLB; плагин берёт локальные трансформы костей, переводит оси glTF обратно в оси игры (они совпадают с осями Max) и ставит линейные ключи. Кватернионы выравниваются по знаку, ступенчатые (STEP) каналы сохраняются ступенчатыми.

Логи: `%LOCALAPPDATA%\CP2077Import\import.log` (последняя операция) и `wolvenkit_last.txt` (вывод последнего вызова WolvenKit). `cp77probe.exe` — то же ядро без Max (`cp77probe plan <ent>`, `cp77probe clips <anims>` и т.д.).

## Настройки

Файл `%LOCALAPPDATA%\CP2077Import\settings.ini` (основное меняется из окон):

| Ключ | По умолчанию | Смысл |
|---|---|---|
| `GamePath`, `WolvenKitCli` | — | папка игры, `WolvenKit.CLI.exe` |
| `CacheDir` | `%LOCALAPPDATA%\CP2077Import\cache` | кэш извлечённых файлов |
| `ExtraArchives` | — | моды: папки или `.archive` через `;` |
| `UnitsPerMeter` | `0` | `0` — по системной единице Max; иначе число (100 = см) |
| `Lod` / `LodFilter` | `1` / `1` | LOD; при `Lod=1` экспортируется только LOD 1 |
| `Textures`, `MaterialType`, `Decals`, `NormalFlipGreen` | `1`, `Physical`, `1`, `0` | материалы |
| `SkinBind` | `Rest` | `Rest` — поза покоя скелета, `Mesh` — бинд-матрицы из GLB |
| `RestPose` | `APose` | `APose` или `Reference` |
| `WeldVertices` | `1` | сшивать UV-швы для гладкого шейдинга |
| `WeaponSlot` | `WeaponRight` | слот оружия |
| `GapFrames` / `FrameRate` | `10` / `30` | пауза между клипами / fps сцены |
| `ExtractTimeout` | `900` | секунд на один вызов WolvenKit |

## Ограничения

- Морф-таргеты голов, cloth и физика не импортируются; scale-треки анимаций не применяются.
- Какое оружие носит NPC, записано в TweakDB — оружие выбирается вручную.
- У многослойных материалов запекается только цвет слоёв (без мелкой фактуры, шероховатости и металличности).
- WolvenKit 9.1 иногда падает со stack overflow на больших animsets. Плагин запускает его копию с увеличенным стеком (`WolvenKit.CLI.cp77.exe` рядом с оригиналом) и повторяет вызов один раз.

---

## Сборка из исходников

**GitHub Actions.** Workflow `Build plugin` собирает плагин для 3ds Max 2027 на Windows-сервере GitHub (SDK скачивается автоматически) при каждом push в `main`; результат — артефакт `CP2077Import-Max2027`. Тег `v*` или коммит со словом `[release]` в сообщении дополнительно публикует релиз (версия — из `package/CP2077Import/PackageContents.xml`).

**Локально.** Нужны 3ds Max SDK нужной версии и Visual Studio 2022/2026 (или Build Tools) с «Desktop development with C++». **`BUILD.bat`** найдёт установленные SDK, соберёт `CP2077Import.dlu` под каждую версию и `cp77probe.exe`, затем вызовет `INSTALL.bat`.
- одна версия: `BUILD.bat 2025`;
- SDK в другом месте: `set MAXSDK_2025=D:\sdk\maxsdk`, затем `BUILD.bat`.

```
src/core/   без SDK: JSON, glTF/GLB, схема JSON WolvenKit, сборка сущности, подготовка сцены,
            материалы и запекание, анимации, драйвер WolvenKit.CLI
src/max/    слой Max: узлы, кости, Skin, материалы, ключи, note track, интерфейс CP77Cpp
scripts/    окна (MAXScript) и меню
tools/      cp77probe
tests/      синтетические файлы в форматах WolvenKit + эталон, заглушка SDK, run_checks.sh
third_party/stb  stb_image / stb_image_write (public domain / MIT)
```

`sh tests/run_checks.sh` (Linux/WSL, g++, python3): компиляция плагина против заглушки SDK для 2022–2027, тесты ядра под ASan/UBSan, проверка MAXScript.

Cyberpunk 2077 — товарный знак CD PROJEKT S.A. Плагин не содержит файлов игры; их извлекает WolvenKit из вашей копии игры.
