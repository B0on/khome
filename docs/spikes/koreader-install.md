# KOReader + Rakuyomi 実機導入ログ

- 実施日: 2026-10-23
- 対象: Kindle Paperwhite 2 / FW 5.12.2.1.1 / armv7l soft-float / RAM 248MB (`free -m` の total)
- 接続: `ssh -o BatchMode=yes kindle '<cmd>'` (root, <kindle-ip>)
- 使用物:
  - KOReader `koreader-kindlepw2-v2026.07.1.zip` (展開済み `vendor/koreader/`)
  - Rakuyomi `rakuyomi-kindle.zip` v1.44.1 (展開済み `vendor/rakuyomi/rakuyomi.koplugin/`)
  - 両 zip の sha256 は `vendor/SHA256SUMS.txt` と一致することを再確認した。

> 端末名について: `vendor/README.md` は「対象端末: Kindle Paperwhite 4 (PW4)」と書いているが、
> 実機の `/proc/usid` は `905A…` で始まり、RAM も 248MB で依頼の値と一致するため、本文では
> 依頼どおり **Paperwhite 2** として扱う。PW4 という記載は vendor/README.md 側の誤りの可能性がある。

## 0. 事前確認

- ローカル展開物の symlink 数: **0**
  ```
  $ find vendor/koreader vendor/rakuyomi -type l | wc -l
  0
  ```
  したがって vfat 上での symlink エラーは起きない。
- 転送前の Kindle 側: `/mnt/us/koreader`、`/mnt/us/extensions/koreader`、
  `/mnt/us/koreader/rakuyomi` はいずれも存在しなかった（クリーン導入）。
- Kindle 側の `/mnt/us` は `fsp` (fuse.fsp) で、下位は `/dev/loop/0` の vfat。
- Kindle 側 `rsync` は `/mnt/us/usbnet/bin/rsync` (v3.2.3) を使用。
- Kindle 側 `file` は `/mnt/us/usbnet/bin/file` (file-5.41)。
- Kindle 側 `timeout` は BusyBox 版で、`timeout 5 cmd` ではなく **`timeout -t 5 cmd`** の書式。

## 1. 転送（実際に打ったコマンド）

ディレクトリを作成:

```sh
ssh -o BatchMode=yes kindle \
  'mkdir -p /mnt/us/koreader/plugins /mnt/us/extensions'
```

KOReader 本体:

```sh
rsync -rltD --no-perms --no-owner --no-group --omit-dir-times \
  --exclude='.DS_Store' \
  -e 'ssh -o BatchMode=yes' \
  --rsync-path=/mnt/us/usbnet/bin/rsync \
  vendor/koreader/koreader/ \
  kindle:/mnt/us/koreader/
```

KUAL 拡張:

```sh
rsync -rltD --no-perms --no-owner --no-group --omit-dir-times \
  --exclude='.DS_Store' \
  -e 'ssh -o BatchMode=yes' \
  --rsync-path=/mnt/us/usbnet/bin/rsync \
  vendor/koreader/extensions/koreader/ \
  kindle:/mnt/us/extensions/koreader/
```

Rakuyomi:

```sh
rsync -rltD --no-perms --no-owner --no-group --omit-dir-times \
  --exclude='.DS_Store' \
  -e 'ssh -o BatchMode=yes' \
  --rsync-path=/mnt/us/usbnet/bin/rsync \
  vendor/rakuyomi/rakuyomi.koplugin/ \
  kindle:/mnt/us/koreader/plugins/rakuyomi.koplugin/
```

結果:

- 3 回とも rsync の終了コードは 0。パーミッション・symlink のエラーは出なかった。
- KOReader 本体は約 2分9秒、Rakuyomi は約 56秒。
- vfat のため Kindle 側の表示パーミッションはすべて `rwxrwxrwx` になるが、
  実行ファイル (`luajit`, `server` など) は実行可能なまま配置されている。

## 2. 転送結果の突き合わせ

ローカル（`stat -f %z` の合計）:

| 対象 | ファイル数 | 合計バイト |
| --- | ---: | ---: |
| `vendor/koreader/koreader/` | 1029 | 86,569,690 |
| `vendor/koreader/extensions/koreader/` | 5 | 9,594 |
| `vendor/rakuyomi/rakuyomi.koplugin/` | 130 | 40,086,695 |

Kindle 側（`find -type f | wc -l` と `stat -c %s` の合計）:

| 対象 | ファイル数 | 合計バイト |
| --- | ---: | ---: |
| `/mnt/us/koreader/`（KOReader + rakuyomi を含む） | 1159 | 126,656,385 |
| `/mnt/us/extensions/koreader/` | 5 | 9,594 |
| `/mnt/us/koreader/plugins/rakuyomi.koplugin/` | 130 | 40,086,695 |

- `1029 + 130 = 1159`、`86,569,690 + 40,086,695 = 126,656,385` で完全一致。
- 主要ファイルの sha256 もローカルと Kindle 側で一致:
  `koreader.sh` = `99135d67…`, `extensions/koreader/menu.json` = `849077d8…`,
  `rakuyomi.koplugin/server` = `f92e74a8…`。
- `du -sh` / `df`:
  ```
  $ du -sh /mnt/us/koreader
  128.0M  /mnt/us/koreader
  $ du -sk /mnt/us/koreader /mnt/us/extensions/koreader
  131072  /mnt/us/koreader
  56      /mnt/us/extensions/koreader
  $ df -h /mnt/us
  Filesystem                Size      Used Available Use% Mounted on
  fsp                       3.2G    192.0M      3.0G   6% /mnt/us
  ```
  （`du` はブロック確保量なので実バイト合計 126.6MB より大きく出る。）

## 3. settings.json

`/mnt/us/koreader/rakuyomi/settings.json` は存在しなかったので、依頼の内容で新規作成した
（既存ファイルの上書きはしていない）。

```sh
ssh -o BatchMode=yes kindle 'set -e
D=/mnt/us/koreader/rakuyomi
if [ -e "$D/settings.json" ]; then echo "EXISTS - not overwriting"; exit 0; fi
mkdir -p "$D"
cat > "$D/settings.json" <<"EOF"
{
  "source_lists": [
    { "url": "https://tachibana-shin.github.io/aidoku-sources-next/index.min.json", "type": "aidoku" },
    { "url": "https://aidoku-community.github.io/sources/index.min.json", "type": "aidoku" },
    { "url": "https://github.com/keiyoushi/extensions/raw/repo/index.pb", "type": "keiyoushi" }
  ],
  "languages": ["ja", "en"]
}
EOF
echo "written"'
```

検証:

```
$ /mnt/us/usbnet/bin/jq -e '.source_lists | length' /mnt/us/koreader/rakuyomi/settings.json
3
$ /mnt/us/usbnet/bin/jq -e '.languages' /mnt/us/koreader/rakuyomi/settings.json
[
  "ja",
  "en"
]
```

配置先の根拠:

- `Paths.lua:7-9` の `getHomeDirectory()` は `DataStorage:getDataDir() .. "/rakuyomi"`。
- `datastorage.lua:42` で Kindle の data_dir は `"."`（KOReader の作業ディレクトリ相対）なので、
  `/mnt/us/koreader/rakuyomi` になる。今回そこに作成した。

キー名・形式の照合:

- `vendor/rakuyomi/rakuyomi.koplugin/` には `settings.schema.json` もドキュメントも入っていない
  （JSON は `BUILD_INFO.json` のみ）。
- プラグイン自身の定義:
  - `SourceListsListing.lua:233` に
    `--- @param source_type "aidoku"|"lnreader"|"mangayomi"|"keiyoushi"` とあり、
    `SourceListsListing.lua:234-239` で `{ url = url, type = source_type }` を
    `settings.source_lists` に追加している。
  - `LanguagesListing.lua:53` 以降が `settings.languages` を配列として扱う。
- サーバーバイナリに埋め込まれた既定設定（`server` の strings）も同じ形:
  `"$schema"`, `"source_lists": [ { "url": ..., "type": "aidoku" }, ... ]`, `"languages": ["en"]`。
- したがって依頼の JSON はプラグインの実装・既定値と一致しており、修正は不要だった。
- 既定値には `$schema` が含まれるが、これはメタデータであり、依頼どおり省略した。

## 4. 実行ファイルと起動確認

### 4.1 KOReader `luajit`

```
$ file /mnt/us/koreader/luajit
/mnt/us/koreader/luajit: ELF 32-bit LSB executable, ARM, EABI5 version 1 (SYSV),
dynamically linked, interpreter /lib/ld-linux.so.3, for GNU/Linux 3.0.35, stripped
```

```
$ eu-readelf -h /mnt/us/koreader/luajit | grep -E 'Class|Machine|Flags'
  Class:   ELF32
  Machine: ARM
  Flags:   0x5000200
```

```
$ eu-readelf -A /mnt/us/koreader/luajit
  Owner          Size
  aeabi            50
    File:          40
      CPU_name: 7-A
      CPU_arch: v7
      CPU_arch_profile: Application
      ARM_ISA_use: Yes
      THUMB_ISA_use: Thumb-2
      VFP_arch: VFPv3
      Advanced_SIMD_arch: NEONv1
      ABI_PCS_wchar_t: 4
      ABI_FP_denormal: Needed
      ABI_FP_exceptions: Needed
      ABI_FP_number_model: IEEE 754
      ABI_align8_needed: Yes
      ABI_enum_size: int
      CPU_unaligned_access: v6
      Virtualization_use: Allowed
```

- `Flags: 0x5000200` = `EF_ARM_EABI_VER5 (0x05000000)` + `EF_ARM_ABI_FLOAT_SOFT (0x200)`。
  hard-float の `0x400` は立っていない。
- `Tag_ABI_VFP_args` は `-A` の出力に存在しない。
- インタプリタは `/lib/ld-linux.so.3`（実体 `ld-2.20.so` への symlink）で、
  `/lib/ld-linux-armhf.so.3` は存在しない。
- 動的依存は `libm.so.6`, `libdl.so.2`, `libgcc_s.so.1`, `libc.so.6` で、いずれも `/lib` に存在する。

### 4.2 Rakuyomi `server`

```
$ file /mnt/us/koreader/plugins/rakuyomi.koplugin/server
...: ELF 32-bit LSB executable, ARM, EABI5 version 1 (SYSV), statically linked, not stripped

$ eu-readelf -h .../server | grep -E 'Class|Machine|Flags'
  Class:   ELF32
  Machine: ARM
  Flags:   0x5000200

$ eu-readelf -A .../server
      CPU_name: 7-A
      CPU_arch: v7
      CPU_arch_profile: Application
      ARM_ISA_use: Yes
      THUMB_ISA_use: Thumb-2
      VFP_arch: VFPv3
      Advanced_SIMD_arch: NEONv1
      ABI_PCS_GOT_use: GOT-indirect
      ABI_PCS_wchar_t: 4
      ABI_FP_rounding: Needed
      ...
```

- `Flags: 0x5000200`（EABI5 + soft-float）、`Tag_ABI_VFP_args` なし。
- `.dynamic` / `.interp` セクションが無く、`file` も `statically linked` と判定。
  → 動的ライブラリの不足は原理的に発生しない。
- 補助バイナリ `cbz_metadata_reader`, `uds_http_request` も同じく
  `ELF 32-bit LSB executable, ARM, EABI5 ... statically linked`。

### 4.3 サーバーの短時間起動

```
$ timeout -t 5 ./server --help
[INFO] starting rakuyomi, version: v1.44.1 (kindle)
Usage: server <HOME_PATH>

Arguments:
  <HOME_PATH>

Options:
  -h, --help  Print help
exit=0

$ timeout -t 5 ./server
[INFO] starting rakuyomi, version: v1.44.1 (kindle)
error: the following required arguments were not provided:
  <HOME_PATH>
...
exit=2
```

- バイナリは起動し、バージョン `v1.44.1 (kindle)` を出力した。
- KOReader 本体 (`koreader.sh` / `reader.lua`) は実行していない。画面描画もしていない。

## 5. `koreader.sh` と起動・終了の調査（実行はしていない）

### 5.1 引数ごとの挙動

`vendor/koreader/koreader/koreader.sh` を読んだ（Kindle 側 `/mnt/us/koreader/koreader.sh` と
sha256 が一致することを確認済み）。結果:

- `--kual` (`82-85`): KUAL 起動扱い。`FROM_KUAL=yes`、`usleep 250000`、nice 値が 5 なら renice、
  起動メッセージを eips に出す (`319-321`)。framework は止めない。
- `--framework_stop` (`92-96`): `STOP_FRAMEWORK=yes`, `NO_SLEEP=no`。
  - Upstart では `stop lab126_gui` を実行し、teardown 待ちに `usleep 1250000` (`215-229`)。
  - このモードでは pillow / awesome / cvm の一時停止処理は通らない（`241` の条件が偽）。
- `--asap` (`97-102`): `NO_SLEEP=yes`、`EIPS_NO_SLEEP=yes`。スリープ無しで起動。
- 上記以外の引数はそのまま `reader.lua "$@"` に渡る (`334`)。`reader.lua` が解釈するのは
  `-d/-v/-p/-h` とファイル／ディレクトリのパスのみ (`reader.lua:77-89, 120-155`)。
- 既定（`--framework_stop` なし、Upstart）:
  - fb を `/var/tmp/koreader-fb.dump` に退避 (`252`)。
  - FW >= 5.6.5 なら `lipc-set-prop com.lab126.pillow disableEnablePillow disable` (`260`)。
  - FW >= 5.7.2 なら `killall -STOP awesome` (`274`)。FW < 5.12.4 なら wmctrl でタイトルバーを縮小 (`266-272`)。
  - `TOGGLED_SERVICES`（`stored webreader kfxreader kfxview todo tmd rcm archive scanner otav3 otaupd`）を `stop` (`78, 296-298`)。
  - `cvm` は「framework を止めない かつ SysV」のときだけ `killall -STOP cvm` (`302-307`)。
    PW2 は Upstart なので、この経路では cvm は止めない。
  - `volumd` は init スクリプトがあれば常に SIGSTOP (`309-314`)。
- 終了時:
  - `--framework_stop` の場合: `start lab126_gui` で framework ごと戻す (`360-367`)。
    この経路では `booklet.home` の明示起動はしない。
  - framework を止めていない場合: 止めたサービスを `start`、`killall -CONT awesome`、
    fb ダンプ書き戻し、`lipc-set-prop ... disableEnablePillow enable`、
    `lipc-set-prop com.lab126.appmgrd start app://com.lab126.booklet.home` (`369-396`)。
  - パスコードを無効化していた場合は戻す (`422-425`)。`/var/tmp/koreader.sh` と
    `/var/tmp/fbink` を削除 (`427-428`)。終了コードは `reader.lua` の戻り値 (`430`)。

### 5.2 起動直後に特定プラグイン／画面を開く方法

- **引数**: 専用の引数は無い。`--kual` / `--framework_stop` / `--asap` は shell が消費し、
  残りは `reader.lua` に渡るが、プラグインや画面を指定するオプションは無い。
- **環境変数**: `KO_HOME`（データディレクトリ変更）、`KO_RTL`、`KO_DONT_MANAGE_NETWORK` などはあるが、
  起動画面を指定する環境変数は無い。
- **`settings.reader.lua` の `start_with`**: 選べるのは
  `filemanager` / `history` / `favorites` / `folder_shortcuts` / `last` のみ
  (`filemanagermenu.lua:987-993`, `reader.lua:255, 282-299`)。Rakuyomi は指定できない。
- **ユーザーパッチ**: `/mnt/us/koreader/patches/` が対象 (`datastorage.lua:79-81`)。
  `reader.lua:26-27` で `0/1-*`、`reader.lua:190` で `2-*`（UIManager 準備後、アプリ起動前）、
  `reader.lua:320` で `8-*`、`reader.lua:325` で `9-*` が適用される。
  `2-*.lua` で `FileManager.showFiles` をフックし、`UIManager:scheduleIn()` で
  `FileManager.instance.rakuyomi:openLibraryView()` を呼ぶ、という方法が考えられる。
  プラグインはディレクトリ名 `rakuyomi` で FileManager に登録される
  (`filemanager.lua:418-424`, `main.lua:47-61, 82`)。
- **Profiles プラグインの自動実行**: `plugins/profiles.koplugin` には
  「on KOReader start」自動実行があり (`main.lua:698, 1156-1160`)、
  遅延実行 `auto_exec_delay` も設定できる (`main.lua:1256-1259`)。
  Rakuyomi は Dispatcher アクション `start_library_view` を登録している
  (`main.lua:36-41`) ので、プロファイルにこのアクションを入れ、
  「on KOReader start」＋数秒の遅延を設定すれば、起動後にライブラリ画面を開ける可能性が高い。
  ただしプラグインはパス順で読み込まれ、`profiles` は `rakuyomi` より先に `init()` されるため、
  遅延を入れないとアクション未登録のタイミングになる。実機での動作確認は未実施。
- **結論**: 引数や `start_with` だけで Rakuyomi を開く手段は無い。
  実現するなら (a) Profiles の起動時自動実行＋遅延、(b) `patches/2-*.lua` のどちらか。
  画面を出さない今回の作業では、どちらも作成・実行していない。

### 5.3 外部から KOReader の終了を知る方法

- **pid ファイルは無い**: `koreader.sh` に pid ファイルの作成は無く、`pidof reader.lua` を
  後始末に使うだけ (`339`)。KOReader 本体も終了通知用の pid ファイルを書いていない。
- **プロセス**: `koreader.sh` は自分を `/var/tmp/koreader.sh` にコピーして `exec` し (`18-22`)、
  その中で `./reader.lua` を前景実行する (`334`)。したがって
  - SSH 経由で起動した場合は、その SSH コマンド自体の終了／終了コードが最も確実な合図。
  - 起動済みなら `pidof reader.lua`（または `luajit`）をポーリングする。
- **lipc イベント**: KOReader 専用の「終了」lipc イベントは無い。
  `--framework_stop` 時は `KindleTouch:exit()` が `com.lab126.blanket load screensaver` を
  呼び (`device.lua:2036-2065`)、その後 `koreader.sh` が `start lab126_gui` を実行する。
  framework を止めない場合は `com.lab126.appmgrd start app://com.lab126.booklet.home` が呼ばれる
  (`koreader.sh:387, 395`)。これらは「終了後の復帰処理」であり、KOReader が発行する
  終了通知ではない。`com.lab126.kaf` の `frameworkStarted` プロパティは
  `STOP_FRAMEWORK=yes` のとき起動時に登録されるもの (`device.lua:367-381`) で、
  終了イベントではない。
- **注意**: 実機の BusyBox には `status` コマンドが無い（`sh: status: not found`）。
  Upstart の状態を見るなら `/sbin/initctl status lab126_gui` など別の手段が必要（未検証）。

### 5.4 `extensions/koreader/menu.json` の KUAL 項目

- `Start KOReader` — `/mnt/us/koreader/koreader.sh --kual`
- `Start KOReader (no framework)` — `/mnt/us/koreader/koreader.sh --kual --framework_stop`
- `Start KOReader (ASAP)` — `/mnt/us/koreader/koreader.sh --kual --asap`
  - Voyage 向けの同名項目だけ `exitmenu: false`
- `Tools`
  - `Update KOReader` — `./bin/koreader-ext.sh update_koreader`
  - `Clean Install of KOReader` — `./bin/koreader-ext.sh install_koreader`
  - `Start SSH server` — `./bin/koreader-ext.sh start_ssh`（dropbear を 2222 番で起動、
    pid は `/tmp/dropbear_koreader.pid`）
  - `Stop SSH server` — `./bin/koreader-ext.sh stop_ssh`
- `config.xml` は `menu.json` を `dynamic="true"` で参照している。

## 6. やっていないこと

- `koreader.sh` / `reader.lua` の実行、KOReader の画面起動テスト。
- `lipc-set-prop`、`mntroot rw`、`/` や `/etc` への書き込み、reboot、プロセスの kill/STOP。
- `/mnt/us/usbnet`、`/mnt/us/extensions/newspaper` など既存拡張の変更。
- リポジトリ内の既存ファイルの変更（このファイルの新規作成のみ）。
