# PS3 CD-DA to LDAC Network Streamer

Проект для прямого чтения Audio CD (CD-DA) из привода PlayStation 3 в обход файловой системы и трансляции чистого PCM-аудиопотока по сети по протоколу UDP на ПК или смартфон.

---

## 1. Сборка `.pkg` одной командой через Docker

Для сборки не требуется локально настраивать компилятор `ppu-gcc`. Используется готовый образ SDK PSL1GHT:

```bash
docker run --rm -v $(pwd):/src -w /src ps3dev/psl1ght make pkg
```

После выполнения команды в корне проекта появится установочный файл **`CDLDAC001.pkg`**.

### Установка на PS3:
1. Скопируйте `CDLDAC001.pkg` на USB-накопитель (FAT32) в корень.
2. На консоли с кастомной прошивкой (CFW Evilnat / Cobra) или PS3HEN перейдите в **Package Manager -> Install Package Files -> Standard**.
3. Установите приложение.
4. Отредактируйте целевой IP-адрес приемника в файле `/dev_hdd0/game/CDLDAC001/USRDIR/config.txt` или поместите `config.txt` в корень флешки:
   ```text
   192.168.1.105:12345
   ```

---

## 2. Запуск на Android через Termux (Без сторонних приложений)

Для вывода звука на подключенные по Bluetooth наушники с кодеком LDAC на телефоне используется связка PulseAudio и Netcat.

### Шаг 1. Установка пакетов в Termux
```bash
pkg update && pkg install pulseaudio sox netcat-openbsd -y
```

### Шаг 2. Однострочник запуска воспроизведения
Запустите в Termux следующую команду:

```bash
pulseaudio --start --exit-idle-time=-1 && nc -l -u -p 12345 | pacat --raw --format=s16le --rate=44100 --channels=2 --latency-msec=60
```

> **Совет по качеству LDAC на Android:**
> В «Параметрах разработчика» Android перейдите в раздел **Аудиокодек Bluetooth** и выберите **LDAC**, а в пункте **Качество воспроизведения LDAC** выберите режим *«Оптимизировано для качества звука (990 кбит/с / 909 кбит/с)»*. Это обеспечит передачу Red Book CD (1411 кбит/с PCM) без артефактов и сжатия частотного диапазона.

---

## 3. Запуск на ПК (Python)

```bash
pip install sounddevice numpy
python3 receiver/receiver.py --port 12345 --buffer 80
```
