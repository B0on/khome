# vendor/ — KOReader と Rakuyomi の取得物

対象端末: Kindle Paperwhite 2 (PW2) / FW 5.12.2.1.1 / armel soft-float (kindlehf 不可)
取得日: 2026-10-23
このディレクトリは「取得と検査」のみ。Kindle へのインストールは行っていない。

## 1. 取得したもの

| 項目 | KOReader | Rakuyomi (tachibana-shin fork) |
| --- | --- | --- |
| バージョン | v2026.07.1 (2026-08-01 公開) | v1.44.1 (2026-10-19 公開) |
| リリースページ | https://github.com/koreader/koreader/releases/tag/v2026.07.1 | https://github.com/tachibana-shin/rakuyomi/releases/tag/v1.44.1 |
| アセット | `koreader-kindlepw2-v2026.07.1.zip` | `rakuyomi-kindle.zip` |
| URL | https://github.com/koreader/koreader/releases/download/v2026.07.1/koreader-kindlepw2-v2026.07.1.zip | https://github.com/tachibana-shin/rakuyomi/releases/download/v1.44.1/rakuyomi-kindle.zip |
| サイズ (bytes) | 40,715,224 | 15,394,033 |
| sha256 | `ea1f575c54492a2c679d128b7f3210fd7d6a87e5f5a1ff1f7a7fe2080ff68f86` | `9255a86e750fd5e62fe856e3602556a83eb43b438c2cd41512abf53e0fe3e155` |

sha256 は `vendor/SHA256SUMS.txt` にも保存した。

### チェックサムの扱い
両リリースとも、リリース本文にもアセット一覧にもチェックサムファイル・ハッシュ値の記載は無かった
(KOReader のアセットは zip/apk/tar/deb のみ、Rakuyomi は zip と `settings.schema.json` のみ)。
そのため公式値との照合はできず、上記 sha256 はローカルで計算した記録値である。
サイズは GitHub API (`/releases/latest` の `assets[].size`) と一致することを確認した。

`vendor/koreader/koreader/git-rev` は `v2026.07.1`。
`vendor/rakuyomi/rakuyomi.koplugin/BUILD_INFO.json` は `{ "version": "v1.44.1", "build": "kindle" }` で、
kindlehf / kindlea9 ではなく無印 kindle ビルドであることを確認した。

## 2. 展開先と構成

```
vendor/
  koreader-kindlepw2-v2026.07.1.zip
  rakuyomi-kindle.zip
  SHA256SUMS.txt
  koreader/                  <- KOReader zip を展開
  rakuyomi/                  <- Rakuyomi zip を展開
```

### KOReader zip の最上位構成
zip エントリは 1034 件、最上位は 2 つだけ (ディレクトリ専用エントリは無し):

- `koreader/` (1029 エントリ) — `koreader.sh`, `luajit`, `reader.lua`, `fbink`, `sdcv`,
  `dbclient`, `wmctrl`, `dropbear`, `tar`, `libs/`, `frontend/`, `plugins/`, `data/`, `fonts/`,
  `l10n/`, `resources/`, `ota/` など
- `extensions/` (5 エントリ) — KUAL 用:
  - `extensions/koreader/config.xml`
  - `extensions/koreader/menu.json`
  - `extensions/koreader/README.txt`
  - `extensions/koreader/bin/koreader-ext.sh`
  - `extensions/koreader/bin/libkohelper.sh`

### Rakuyomi zip の最上位構成
zip エントリは 184 件、最上位は `rakuyomi.koplugin/` 1 つのみ。
中に `main.lua`, `_meta.lua`, `Paths.lua`, `Settings.lua`, `server`, `cbz_metadata_reader`,
`uds_http_request`, `l10n/`, `platform/`, `widgets/`, `jobs/` など。
KOReader 側では `koreader/plugins/rakuyomi.koplugin/` に置く構成 (docs の指示どおり)。

## 3. アーキテクチャ検査 (soft-float / armel の確認)

`readelf` はこの Mac に無く、`brew` は使わない方針のため、既存の `.venv` に pyelftools 0.33 を入れ、
同梱の `.venv/bin/readelf.py` で確認した (`readelf -A` 相当)。

### KOReader 主要バイナリ

```
$ file vendor/koreader/koreader/luajit
vendor/koreader/koreader/luajit: ELF 32-bit LSB executable, ARM, EABI5 version 1 (SYSV), dynamically linked, interpreter /lib/ld-linux.so.3, for GNU/Linux 3.0.35, stripped

$ .venv/bin/readelf.py -h vendor/koreader/koreader/luajit | grep Flags
  Flags:                             0x5000200, Version5 EABI, soft-float ABI

$ .venv/bin/readelf.py -A vendor/koreader/koreader/luajit
Attribute Section: aeabi
File Attributes
  Tag_CPU_name: "7-A"
  Tag_CPU_arch: v7
  Tag_CPU_arch_profile: Application
  Tag_ARM_ISA_use: Yes
  Tag_THUMB_ISA_use: Thumb-2
  Tag_FP_arch: VFPv3
  ...
  Tag_ABI_VFP_args は出力されない (= hard-float の引数受渡しを使っていない)
```

`Tag_FP_arch: VFPv3` は VFP 命令の使用可否で、呼び出し規約は `e_flags` の soft-float と
`Tag_ABI_VFP_args` の不在から soft-float と判断できる。

`fbink`, `sdcv`, `dbclient`, `wmctrl` も同じ結果:

```
vendor/koreader/koreader/fbink:    ELF 32-bit LSB executable, ARM, EABI5 ... Flags: 0x5000200, Version5 EABI, soft-float ABI
vendor/koreader/koreader/sdcv:     ELF 32-bit LSB executable, ARM, EABI5 ... Flags: 0x5000200, Version5 EABI, soft-float ABI
vendor/koreader/koreader/dbclient: ELF 32-bit LSB executable, ARM, EABI5 ... Flags: 0x5000200, Version5 EABI, soft-float ABI
vendor/koreader/koreader/wmctrl:   ELF 32-bit LSB executable, ARM, EABI5 ... Flags: 0x5000200, Version5 EABI, soft-float ABI
```

`vendor/` 配下の全 ELF を走査した結果 (pyelftools):

```
ELF files: 55
  e_machine=EM_ARM e_flags=0x5000200 -> 55 files
files declaring TAG_ABI_VFP_ARGS: NONE (soft-float calling convention)
```

0x5000200 = `EF_ARM_EABI_VER5 (0x05000000) | EF_ARM_ABI_FLOAT_SOFT (0x200)`。
hard-float の `EF_ARM_ABI_FLOAT_HARD (0x400)` を持つファイルは 1 つも無い。

### Rakuyomi のサーバーバイナリ

```
$ file vendor/rakuyomi/rakuyomi.koplugin/server
vendor/rakuyomi/rakuyomi.koplugin/server: ELF 32-bit LSB executable, ARM, EABI5 version 1 (SYSV), statically linked, not stripped

$ .venv/bin/readelf.py -h vendor/rakuyomi/rakuyomi.koplugin/server | grep -E 'Type|Machine|Flags'
  Type:                              EXEC (Executable file)
  Machine:                           ARM
  Flags:                             0x5000200, Version5 EABI, soft-float ABI

$ .venv/bin/readelf.py -d vendor/rakuyomi/rakuyomi.koplugin/server
There is no dynamic section in this file.

$ .venv/bin/readelf.py -A vendor/rakuyomi/rakuyomi.koplugin/server | grep Tag_ABI_VFP_args
(出力なし)
```

補助バイナリも同じ (静的リンク・soft-float):

```
vendor/rakuyomi/rakuyomi.koplugin/cbz_metadata_reader: ELF 32-bit LSB executable, ARM, EABI5 ... statically linked, not stripped
vendor/rakuyomi/rakuyomi.koplugin/uds_http_request:    ELF 32-bit LSB executable, ARM, EABI5 ... statically linked, not stripped
```

`readelf -l` で `Requesting program interpreter` は出ず、`-d` で dynamic section も無いため静的リンク。
(サーバーバイナリは 34,648,744 bytes と大きいが、Rust の静的リンクとしては妥当な範囲。)

### zip の健全性

```
$ unzip -t vendor/koreader-kindlepw2-v2026.07.1.zip | tail -1
No errors detected in compressed data of vendor/koreader-kindlepw2-v2026.07.1.zip.
$ unzip -t vendor/rakuyomi-kindle.zip | tail -1
No errors detected in compressed data of vendor/rakuyomi-kindle.zip.
```

## 4. KOReader の起動と終了 (frameworkless 起動 / 戻り先)

### 起動コマンド (KUAL から)

`extensions/koreader/menu.json`:

- 7-15行: `Start KOReader` → `/mnt/us/koreader/koreader.sh` に `params: "--kual"` (framework は止めない)
- 26-33行: `Start KOReader (no framework)` → `params: "--kual --framework_stop"`
- 34-52行: `Start KOReader (ASAP)` → `params: "--kual --asap"` (Voyage だけ `exitmenu: false`)
- 57-92行: Tools (Update / Clean Install / SSH start・stop) は `./bin/koreader-ext.sh`

つまり素の起動は
`/mnt/us/koreader/koreader.sh --kual --framework_stop`
で、これが「frameworkless 起動」に相当する。

### `koreader/koreader.sh` の主要動作

- 18-22行: 自分自身を `/var/tmp/koreader.sh` にコピーして `exec` (vfat+fuse 対策、OTA 後の再起動判定用)
- 68行: `STOP_FRAMEWORK="no"` が既定
- 91-96行: 第1引数 `--framework_stop` で `STOP_FRAMEWORK="yes"`、`NO_SLEEP="no"`
- 97-102行: `--asap` はスリープ無しで即起動
- 214-229行: `STOP_FRAMEWORK=yes` のとき framework を停止
  - SysV: 219行 `/etc/init.d/framework stop`
  - Upstart (PW2 はこちら): 223行 `stop lab126_gui`、続けて 225行 `usleep 1250000` で teardown 待ち
- 241-299行: framework を止めない場合の処理
  - 260行 `lipc-set-prop com.lab126.pillow disableEnablePillow disable` (FW>=5.6.5)
  - 274行 `killall -STOP awesome` (FW>=5.7.2)
  - 296-298行: `TOGGLED_SERVICES` (78行: stored webreader kfxreader kfxview todo tmd rcm archive scanner otav3 otaupd) を `stop`
- 329-336行: `RETURN_VALUE=85` の間ループし、334行で本体を起動
  `./reader.lua "$@" >>crash.log 2>&1`
- 359-367行: **終了時、framework を止めていた場合の戻り先**
  - SysV: 363行 `cd / && /etc/init.d/framework start`
  - Upstart: 365行 `cd / && start lab126_gui`
- 369-396行: **終了時、framework を止めていない場合**
  - 372-374行: 止めたサービスを `start` で戻す
  - 379行: `killall -CONT awesome`
  - 384-387行: fb ダンプを書き戻し、`lipc-set-prop com.lab126.pillow disableEnablePillow enable`、
    387行 `lipc-set-prop com.lab126.appmgrd start app://com.lab126.booklet.home`
  - 394-395行: ステータスバー復帰と `appmgrd start app://com.lab126.booklet.home`
- 422-425行: 起動時に消した passcode を戻す
- 427-428行: `/var/tmp/koreader.sh` と `/var/tmp/fbink` を削除

要約: `--framework_stop` で起動した場合は、終了時に `start lab126_gui` (Upstart) で
ホーム UI ごと戻す。`--framework_stop` 無しの場合は framework を動かしたまま awesome/pillow を
一時停止し、終了時に `appmgrd start app://com.lab126.booklet.home` で本のホーム画面に戻す。

## 5. Rakuyomi の設定ファイル

### 置き場所 (Kindle)

- `rakuyomi.koplugin/Paths.lua:7-9`
  `Paths.getHomeDirectory()` = `DataStorage:getDataDir() .. "/rakuyomi"`
- `koreader/datastorage.lua:41-42`: Kindle では `data_dir = "."` (KOReader の作業ディレクトリ相対)
- `koreader/frontend/device/kindle/device.lua:403`: `home_dir = "/mnt/us"`
  → 実体は **`/mnt/us/koreader/rakuyomi/`**
- `rakuyomi.koplugin/platform/generic_unix_platform.lua:333-337`:
  サーバーを `plugins/rakuyomi.koplugin/server <home_dir>` として起動 (argv[1] がホーム)
- サーバー側 `backend/server/src/app.rs:206`: `home_path.join("settings.json")` を読み書き。
  無ければ同梱の default-settings.json を書き出す (app.rs:206, 211-216)

したがって設定ファイルは **`/mnt/us/koreader/rakuyomi/settings.json`**。
同じフォルダに `database.db`, `sources/`, `downloads/` も作られる。
プラグイン本体は `/mnt/us/koreader/plugins/rakuyomi.koplugin/`。

### docs の記載

`docs/src/user-guide/installation/installing-to-your-device.md` (v1.44.1 のソース):
- 5-11行: `settings.json` は source lists と languages を持つ。初回起動時に既定値が自動生成される
- 15-17行: 既定設定は `backend/server/assets/default-settings.json` を include
- 32-33行: `rakuyomi.koplugin` を KOReader の `plugins/` にコピー
- 36-43行: カスタムする場合のみ `koreader/rakuyomi/` を作り `settings.json` を置く
  (Kindle の KOReader は `koreader/`)

既定値 `backend/server/assets/default-settings.json`:

```json
{
  "$schema": "https://github.com/tachibana-shin/rakuyomi/releases/latest/download/settings.schema.json",
  "source_lists": [
    { "url": "https://tachibana-shin.github.io/aidoku-sources-next/index.min.json", "type": "aidoku" },
    { "url": "https://aidoku-community.github.io/sources/index.min.json", "type": "aidoku" },
    { "url": "https://github.com/lnreader/lnreader-plugins", "type": "lnreader" }
  ],
  "languages": ["en"]
}
```

### 日本語ソースを使う設定例

docs が示すのは「`languages` に希望言語を並べる」ところまでで、特定の日本語ソース名の
記載は無い (docs 内に "Japanese"/"ja" の記述は無し)。既定の source_lists は
Aidoku のコミュニティインデックスを含むので、`languages` に `ja` を足せば
日本語ソースが一覧に出る。docs の既定値をベースにした例:

```json
{
  "$schema": "https://github.com/tachibana-shin/rakuyomi/releases/latest/download/settings.schema.json",
  "source_lists": [
    { "url": "https://tachibana-shin.github.io/aidoku-sources-next/index.min.json", "type": "aidoku" },
    { "url": "https://aidoku-community.github.io/sources/index.min.json", "type": "aidoku" },
    { "url": "https://github.com/lnreader/lnreader-plugins", "type": "lnreader" }
  ],
  "languages": ["ja", "en"]
}
```

`languages` はプラグインの Languages 画面 (`G_reader_settings` の `rakuyomi_languages`) と
同じ値で、ソース一覧の絞り込みに使われる
(`rakuyomi.koplugin/AvailableSourcesListing.lua:198-201, 254-273`)。
空配列にすると絞り込み無し。

参考: 上記 2 つの既定インデックスを実際に取得して `languages` に `ja` を含むソースを確認すると
`ja.comicdays` (コミックDAYS), `ja.shonenjumpplus` (少年ジャンプ＋), `ja.mangadex` (MangaDex),
`ja.rawkuma`, `ja.rawfree` などがあった。これは docs の記載ではなく、インデックス JSON の実データ。

## 6. 注意

- このディレクトリのものは未インストール。`/Volumes/Kindle` への書き込みは一切していない。
- `vendor/*.zip` と展開先 (`vendor/koreader/`, `vendor/rakuyomi/`) は `.gitignore` で除外している。
  `vendor/README.md` と `vendor/SHA256SUMS.txt` は追跡対象のまま。
