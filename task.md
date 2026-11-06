# ТЕХНИЧЕСКОЕ ЗАДАНИЕ (SPECIFICATION PROMPT) ДЛЯ АВТОНОМНОГО АГЕНТА РАЗРАБОТКИ
## Проект: PS3 Audiophile CD Player (Homebrew Standalone Application)

---

### 1. ОБЩИЕ СВЕДЕНИЯ И ЦЕЛЬ ПРОЕКТА
* **Название проекта:** PS3 Audiophile CD Player (`ps3-cd-player`)
* **Платформа:** Sony PlayStation 3 (CFW Evilnat/Cobra и PS3HEN)
* **Формат поставки:** Установочный пакет `.pkg` (TitleID: `CDPL00001`)
* **Цель:** Создать самостоятельное homebrew-приложение на C/C++ для прямого воспроизведения компакт-дисков формата Audio CD (Red Book CD-DA) со встроенным автоматическим получением метаданных (MusicBrainz API), загрузкой обложек альбомов высокого разрешения (Cover Art Archive) и аудиофильским интерфейсом (треклист, обложка, FFT-визуализатор).

---

### 2. СТЕК ТЕХНОЛОГИЙ И ЗАВИСИМОСТИ
1. **Toolchain & SDK:**
   * `PS3DEV / PSL1GHT v2 SDK` (компилятор `powerpc-ps3-elf-gcc`).
   * Сборка пакетов: утилиты `make_self`, `make_fself`, `make_package_npdrm` или `pkg_builder`.
2. **Графика и GUI:**
   * Библиотека **Tiny3D** или **SDL2 for PS3** (аппаратный рендеринг через RSX).
   * Рендеринг шрифтов: `FreeType2` (или встроенный в Tiny3D растровый/TTF рендерер).
3. **Декодирование графики:**
   * `stb_image.h` (single-header) для парсинга JPEG и PNG обложек в текстуры RGBA.
4. **Аудиовывод:**
   * Нативный интерфейс `cellAudio` / `sys_audio` (16-bit PCM Stereo, 44.1 kHz, прямое вещание в HDMI / TOSLINK).
5. **Сетевой стек и парсинг:**
   * `sys_net` (сокеты BSD) / легковесный HTTP(S) клиент (на базе `mbedtls` или сокетов с HTTP 1.1).
   * Парсинг метаданных: библиотека `cJSON` (single-header).
6. **Криптография / Хэширование:**
   * Алгоритм SHA-1 (для вычисления MusicBrainz DiscID) и MD5 (для кэша).

---

### 3. МОДУЛЬНАЯ АРХИТЕКТУРА И ФУНКЦИОНАЛЬНЫЕ ТРЕБОВАНИЯ

#### Модуль 1: Привод и чтение Audio CD (`src/cd_drive.c`, `src/cd_drive.h`)
* **Инициализация:** Открытие дескриптора оптического привода (`/dev_bdvd`) через вызовы `sys_storage_open()`.
* **Чтение оглавления (TOC):**
  * Отправка SCSI-команды `READ TOC` (код `0x43`) через `sys_storage_send_device_command()`.
  * Извлечение: первого трека, последнего трека, LBA-смещения Lead-Out сектора и начального LBA каждого аудио-трека.
* **Чтение аудиопотока:**
  * Чтение сырых секторов CD-DA (Red Book, 2352 байта на сектор) по LBA.
  * Реализация кольцевого буфера (Ring Buffer) на 2–4 секунды аудио (около 1 МБ RAM), чтобы исключить прерывания при вибрациях или параллельных операциях интерфейса.
* **События привода:**
  * Обработка извлечения диска (Eject / Disc Removed) и вставки нового диска (Disc Inserted) без зависания приложения.

#### Модуль 2: Генерация идентификаторов диска (`src/discid.c`, `src/discid.h`)
* **MusicBrainz DiscID:**
  1. Форматирование строки TOC: `FirstTrack LastTrack LeadOut_LBA Track1_LBA Track2_LBA ...` в шестнадцатеричном виде с фиксированной шириной полей (100 слотов).
  2. Вычисление SHA-1 хэша от получившейся строки.
  3. Кодирование хэша в Base64 URL-safe (с заменой `+` на `.`, `/` на `_`, `=` на `-`).
* **Резервный FreeDB / gnudb DiscID:**
  * Расчёт 32-битного чексумма CDDB (сумма цифр секунд смещений треков) на случай, если диск не найден в MusicBrainz.

#### Модуль 3: Сеть и метаданные (`src/metadata.c`, `src/metadata.h`)
* **MusicBrainz REST API:**
  * Endpoint: `GET /ws/2/discid/<DISCID>?fmt=json&inc=recordings+artists` (User-Agent: `PS3CDPlayer/1.0.0 (contact@homebrew.ps3)`).
  * Извлечение:
    * Название релиза (Album Title).
    * Исполнитель (Artist Name / Release Artist).
    * Год выпуска (Release Date / Year).
    * Список треков: номер, название (Title), длительность в миллисекундах.
    * Release MBID (UUID релиза) для запроса обложки.
* **Cover Art Archive (Обложки альбомов):**
  * Endpoint: `GET https://coverartarchive.org/release/<RELEASE_MBID>/front-500` (разрешение 500x500).
  * Загрузка бинарных данных JPEG/PNG в буфер.
  * Сохранение в локальный кэш: `/dev_hdd0/game/CDPL00001/USRDIR/cache/<DISCID>.jpg`.
* **Фоллбэк:**
  * Если соединение с интернетом отсутствует или диск не найден в базе, выводить: `Unknown Album`, `Track 01`, `Track 02`, длительность рассчитывать из секторов TOC, а вместо обложки использовать встроенную текстуру виниловой пластинки / CD.

#### Модуль 4: Аудиодвижок (`src/audio_engine.c`, `src/audio_engine.h`)
* Запуск отдельного потока PPU (`sys_ppu_thread_create`).
* Потоковая подача 16-bit 44.1 kHz Stereo PCM данных в аудиопорт через `cellAudioPortWrite()`.
* Поддержка состояний: `PLAYING`, `PAUSED`, `STOPPED`, `SEEKING`.
* Функции:
  * `audio_play_track(int track_number)`
  * `audio_pause()` / `audio_resume()`
  * `audio_seek(int seconds)`
  * `audio_set_volume(float volume)`
* Экспорт последних сэмплов (текущего кадра) для модуля визуализатора.

#### Модуль 5: Визуализатор (`src/visualizer.c`, `src/visualizer.h`)
* Выполнение быстрого преобразования Фурье (Cooley-Tukey FFT) на блоке 512 или 1024 сэмплов.
* Сглаживание амплитуд по полосам частот (от 20 Гц до 20 кГц, 16–32 полосы).
* Отрисовка динамических спектральных столбиков (с эффектом пиковых маркеров / fall-off).

#### Модуль 6: Пользовательский интерфейс (`src/ui.c`, `src/ui.h`)
* **Разрешение:** 1080p (1920x1080) с автоматическим масштабированием под 720p.
* **Компоновка экрана:**
  * **Слева:** большая обложка альбома (500x500px, стилизованная рамка / тень), под ней статус (Play/Pause, текущее время трека / общее время).
  * **Справа:** название альбома (крупно), имя исполнителя, год выпуска, интерактивный прокручиваемый список треков. Текущий трек подсвечен.
  * **Внизу:** прогресс-бар трека и окно спектрального анализатора.
* **Управление DualShock 3:**
  * **✕** / **Start** — Старт / Пауза.
  * **Up / Down** — навигация по треклисту.
  * **✕ на треке** — мгновенный переход к выбранному треку.
  * **L1 / R1** — предыдущий / следующий трек.
  * **L2 / R2 (удержание)** — плавная перемотка назад / вперед.
  * **△** — переключение режима экрана (полноэкранная обложка + визуализатор / подробный треклист).
  * **Select** — принудительное обновление метаданных из сети.

---

### 4. СТРУКТУРА КАТАЛОГА ПРОЕКТА
```text
ps3-cd-player/
├── Makefile
├── pkg/
│   ├── ICON0.PNG             # Иконка приложения для XMB (320x176)
│   ├── PIC1.PNG              # Фон для XMB (1920x1080)
│   ├── SND0.AT3              # (Опционально) фоновый звук
│   └── package.conf          # Конфиг для сборщика PKG
├── include/
│   ├── cJSON.h
│   └── stb_image.h
├── src/
│   ├── main.c                # Точка входа, главный цикл приложения
│   ├── cd_drive.c            # Работа со SCSI приводом PS3
│   ├── cd_drive.h
│   ├── discid.c              # Алгоритмы MusicBrainz и FreeDB DiscID
│   ├── discid.h
│   ├── metadata.c            # HTTP-клиент, запросы к MusicBrainz и CoverArtArchive
│   ├── metadata.h
│   ├── audio_engine.c        # PPU аудио-поток, кольцевой буфер, cellAudio
│   ├── audio_engine.h
│   ├── visualizer.c          # FFT спектроанализатор
│   ├── visualizer.h
│   ├── ui.c                  # Графический интерфейс и анимации (Tiny3D/SDL2)
│   ├── ui.h
│   ├── input.c               # Опрос геймпада DualShock 3 (ioPad)
│   └── input.h
└── assets/
    ├── font.ttf              # Шрифт с поддержкой кириллицы и латиницы (UTF-8)
    └── default_cover.png     # Заглушка обложки (винил/диск)
```

---

### 5. КЛЮЧЕВЫЕ ТЕХНИЧЕСКИЕ ДЕТАЛИ И РЕФЕРЕНСЫ ДЛЯ АГЕНТА

#### 1. Чтение TOC привода (SCSI команда READ TOC):
```c
#include <sys/storage.h>

// SCSI команда 0x43 (READ TOC)
int read_disc_toc(uint64_t dev_id, cd_toc_t *out_toc) {
    uint8_t cdb[10] = {0};
    cdb[0] = 0x43;       // READ TOC
    cdb[1] = 0x02;       // Format 2: Session info / Full TOC
    cdb[6] = 0x01;       // First track
    cdb[7] = sizeof(cd_toc_buffer) >> 8;
    cdb[8] = sizeof(cd_toc_buffer) & 0xFF;
    
    // Вызов sys_storage_send_device_command с кодом для оптического привода
    return sys_storage_send_device_command(dev_id, ...);
}
```

#### 2. Формула MusicBrainz DiscID:
* `DiscID = Base64_URLSafe(SHA1(header_string))`
* `header_string`: `%02X %02X %08X [100 x %08X]`
  (First track, Last track, LeadOut sector LBA, затем смещения всех треков, дополненные нулями до 100 элементов).

#### 3. Сетевой User-Agent:
* MusicBrainz API требует явный `User-Agent`. Запросы без информативного User-Agent отсекаются с кодом 403 Forbidden.

#### 4. Кодировка строк:
* Обязательна поддержка **UTF-8**. Шрифтовой движок (FreeType) должен загружать шрифт с покрытием Unicode (включая кириллицу).

---

### 6. ЭТАПЫ РАЗРАБОТКИ ДЛЯ АГЕНТА (MILESTONES)
1. **Milestone 1 (Драйвер привода):** Собрать минимальный тест чтения TOC диска и вывода названий дорожек по секторам в консоль отладки (`printf` в `socat` / лог).
2. **Milestone 2 (Аудиопоток):** Запустить непрерывное воспроизведение Red Book PCM через `cellAudioPortWrite` с управлением паузой.
3. **Milestone 3 (Метаданные & Обложки):** Реализовать расчет MusicBrainz DiscID, сетевой запрос к API, сохранение `front-500.jpg` в кэш и парсинг JSON.
4. **Milestone 4 (GUI & Визуализатор):** Отрендерить экран 1080p с текстурой обложки, списком треков и FFT-спектроанализатором.
5. **Milestone 5 (Упаковка):** Настроить `Makefile` для сборки готового `.pkg`, протестировать обработку граничных случаев (отсутствие диска, обложки, интернета).
