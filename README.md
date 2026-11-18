# waypoint_tools

waypoint を記録・編集し、順に Nav2 の `NavigateToPose` に送って走行させる ROS 2 パッケージです。
どの点を目指すか・どこで止まるかはこのパッケージが管理し、Nav2 の `waypoint_follower` は使いません。

## 使い方の流れ

| 手順 | 起動 | 主な設定 |
|---|---|---|
| 1. 記録 | `ros2 launch waypoint_tools record.launch.py` | `record_waypoint_path` |
| 2. 編集 | `ros2 launch waypoint_tools edit.launch.py` | `edit_waypoint_path`, `map_yaml_path` |
| 3. 送信（走行） | `ros2 launch waypoint_tools send.launch.py` | `send_waypoint_path` |

## ビルド

```bash
cd ~/ros2_ws
colcon build --symlink-install --packages-select waypoint_tools
```

## 設定ファイル

設定はすべて `config/params/waypoint_tools_params.yaml` に書きます。パスは以下の書式で書けます。

| 書式 | 解決先 |
|---|---|
| `$(find-pkg-share <package>)/<rel>` | その package の share ディレクトリ基準 |
| `config/...` | `src/waypoint_tools` 基準 |
| `~/...` | ホーム展開 |
| `/abs/path` | 絶対パス |

## 1. 走行しながら waypoint を記録する

ロボットを teleop で走らせると、経路上に waypoint が自動で打たれます。
自己位置の出し方は `localization` で選びます。

| `localization` | 自己位置 | 準備 |
|---|---|---|
| `false` | 別途起動した SLAM / localization の `map → base_link` | slam_toolbox などを別ターミナルで起動（その `/map` を RViz が表示） |
| `true` | この launch が既存の地図で自己位置推定する | 下の「既存の地図で自己位置推定する」を参照 |

| パラメータ | 意味 |
|---|---|
| `record_waypoint_path` | 保存先の YAML ファイル（保存時に上書き） |
| `distance_interval` [m] | この距離進んだら打点 |
| `yaw_interval_deg` [deg] | 進行方位がこれだけ変化したら打点 |
| `min_move` [m] | 直前の打点からこの距離未満では、方位が変化しても打点しない（その場旋回や位置の揺れによる密集を防ぐ） |

```bash
ros2 launch waypoint_tools record.launch.py
```

- 記録開始直後は打点せず、動き出してしきい値を超えてから最初の点が置かれます。
- Ctrl-C で終了すると、停止位置に終端の waypoint を 1 点打ってから `record_waypoint_path` に保存します。

### 既存の地図で自己位置推定する

`localization: true` にすると、`localization_method` に応じて推定器を起動します。

| `localization_method` | 起動するもの | 地図 |
|---|---|---|
| `emcl2` | `emcl2.launch.py` | `map_yaml_path`（2D） |
| `ndt` | `ndt_localization.launch.py` | `ndt_map_path`（PCD）。`map_yaml_path` は RViz 表示用（PCD と同じ座標の地図） |

```yaml
localization: true
localization_method: ndt   # emcl2 / ndt
localization_params_path: $(find-pkg-share orne_box_navigation_executor)/config/params/nav2_params.yaml
map_yaml_path: $(find-pkg-share orne_box_navigation_executor)/config/maps/tsudanuma/tsudanuma.yaml
ndt_map_path: $(find-pkg-share orne_box_navigation_executor)/config/maps/tsudanuma/tsudanuma.pcd   # ndt のみ
```

起動後、RViz の `2D Pose Estimate` で初期位置を与えるまで打点しません。

## 2. waypoint を編集する

`edit_waypoint_path` に編集する YAML、`map_yaml_path` に表示する地図を指定して起動します。

```bash
ros2 launch waypoint_tools edit.launch.py
```

RViz の `Interact` ツールで操作します。

| 操作 | 動作 |
|---|---|
| 円盤をドラッグ | 位置を移動 |
| 矢印をドラッグ | yaw を回転 |
| 右クリック → `insert after` | 直後に waypoint を追加 |
| 右クリック → `delete` | その waypoint を削除 |
| 右クリック → `save` | 開いている YAML に保存 |
| 右クリック → `stop` | チェックで停止点（`stop: true`）、外すと通過点。停止点は赤い円盤と `(stop)` で表示。ファイルへの反映には `save` が必要 |
| 右クリック → `task` → `none` / 識別子 | 対象点の `on_arrival_task` を選択。選択肢は開いている YAML の `arrival_services` から作る |
| 右クリック → `action` → `start` / `stop` | 選択した task の開始・停止を設定。先に task を選ぶ。既定は `start` |

task と action は点のラベルに `[A:start]` のように表示されます。変更した設定は
右クリック → `save` で waypoint YAML に保存します。`task` の `none` は task と action を
両方削除します。新しい識別子をメニューに追加するには YAML の `arrival_services` に
登録してから `edit` ノードの `~/reload` を呼ぶか、編集ノードを起動し直します。

## 3. Nav2 に waypoint を送信して走行する

`waypoint_sender_node` は waypoint を 1 点ずつ Nav2 の `NavigateToPose` に送ります。

| 点の種類 | 動作 |
|---|---|
| 通過点 | ロボットが `switch_radius` [m] 以内に近づいたら次の点を送り、**止まらずに**走る |
| 停止点（`stop: true`） | 到達して停止し、`Next WP` を待つ |
| 最後の点 | 到達して停止し、走行を終了する |
| `on_arrival_task` を持つ点 | Nav2 の到着成功後、識別子の `std_srvs/srv/SetBool` を一度呼ぶ。`start` なら `true`、`stop` なら `false`。成功応答後は `Next WP` を待つ（最終点なら終了） |

### 到着時に別ノードの動作を開始・停止する

YAML の `arrival_services` で識別子ごとに制御サービスを一つ登録し、対象の
waypoint に `on_arrival_task` と `on_arrival_action` を記入します。通常の通過点とは異なり、Nav2 の
`NavigateToPose` が `SUCCEEDED` を返すまで待ってからサービスを呼びます。
`stop: true` を併記する必要はありません。

```yaml
arrival_services:
  A: /mission/control_A
  B: /mission/control_B
waypoints:
  - {x: 1.0, y: 0.0, yaw: 0.0}
  - x: 2.0
    y: 1.0
    yaw: 1.57
    on_arrival_task: A
    on_arrival_action: start
  - x: 3.0
    y: 1.0
    yaw: 0.0
    on_arrival_task: B
    on_arrival_action: start
  - x: 4.0
    y: 1.0
    yaw: 0.0
    on_arrival_task: A
    on_arrival_action: stop
```

各ノードで対応する `/mission/control_A` などの `std_srvs/srv/SetBool` を一つ提供します。
送信側が `start` を `data: true`、`stop` を `data: false` に変換します。
最小限の受信側は `examples/arrival_service_node.py` を参照してください。
同じファイルを異なる `task_id`、`service_name` で起動すると、別ノードになります。
識別子を増やす際に sender のコードを変更する必要はありません。YAML に識別子とサービス名を
追加し、対応する受信サービスを起動します。`on_arrival_action` を省略すると `start` です。
識別子が `arrival_services` にない場合は送信ノードの起動時にエラーになります。
従来の `A: {start: /mission/start_A, stop: /mission/stop_A}` と
`on_arrival_service: /name` は `std_srvs/srv/Trigger` として使用できます。
同じ点に `on_arrival_task` と `on_arrival_service` の両方は指定できません。
受信ノードの**処理**を停止する方式です。ノードのプロセスはサービスを受けるため起動したままです。
プロセスの終了・再起動には別の管理ノードや lifecycle 制御が必要です。
受信側を先に起動してから走行を開始します。成功応答後は RViz パネルの `Next WP`
で次へ進みます。サービスが見つからない、`success: false` が返る、または応答が
10 秒以上ない場合はその点で停止します。`Start` で先頭から再実行できます。
`Next WP` は失敗した点を飛ばします。タイムアウト後に受信側の処理が完了しても
送信側には成功とみなされないため、受信側の処理は重複実行に注意してください。

```bash
# それぞれ別ターミナルで起動（ROS 2 環境を source 済み）
python3 examples/arrival_service_node.py --ros-args -r __node:=arrival_A -p task_id:=A -p service_name:=/mission/control_A
python3 examples/arrival_service_node.py --ros-args -r __node:=arrival_B -p task_id:=B -p service_name:=/mission/control_B
# サービスの存在と型を確認
ros2 service type /mission/control_A
```

### arrival_service_node.py の検証手順

このスクリプトは `std_srvs/srv/SetBool` のサービスを一つ提供し、`data: true` で
処理を開始、`data: false` で停止します。例では処理中に 1 秒ごとにログを出します。
ノードのプロセスは待機を続けるため、停止後も再び開始できます。

以下はリポジトリ内の `examples/arrival_service_node.py` を使う例です。
スクリプトを `script/` に配置した場合は、コマンド中の `examples/` を `script/` に
置き換えてください。いずれのターミナルでも ROS 2 とワークスペースを source します。

**1. 受信側を起動する（ターミナル 1）**

```bash
cd ~/ros2_ws
source /opt/ros/humble/setup.bash
source install/setup.bash
cd src/waypoint_tools
python3 examples/arrival_service_node.py --ros-args -p task_id:=A -p service_name:=/mission/control_A
```

起動時に `タスク A: /mission/control_A を待機中` と表示されます。

**2. Nav2 を使わずに開始・停止を確認する（ターミナル 2）**

```bash
source /opt/ros/humble/setup.bash
source ~/ros2_ws/install/setup.bash
ros2 service type /mission/control_A
ros2 service call /mission/control_A std_srvs/srv/SetBool "{data: true}"
```

型が `std_srvs/srv/SetBool` で、応答が `success=True, message='動作中'` なら開始に成功です。
ターミナル 1 に `タスク A を開始` と約 1 秒ごとの `タスク A が動作中` が出ます。

```bash
ros2 service call /mission/control_A std_srvs/srv/SetBool "{data: false}"
```

応答が `success=True, message='停止中'` となり、定期ログが止まれば停止に成功です。
再度 `data: true` を送れば再開できます。

**3. waypoint 到着から確認する**

Nav2 から到達可能な二つの地点を YAML に用意します。座標は使用する地図に合わせて
変更してください。

```yaml
arrival_services:
  A: /mission/control_A
waypoints:
  - {x: 1.0, y: 0.0, yaw: 0.0, on_arrival_task: A, on_arrival_action: start}
  - {x: 2.0, y: 0.0, yaw: 0.0, on_arrival_task: A, on_arrival_action: stop}
```

この YAML を、例えば `config/waypoints/service_test.yaml` として保存します。
ターミナル 1 の受信側と Nav2 を起動したまま、別ターミナルで送信側を起動します。

```bash
source /opt/ros/humble/setup.bash
source ~/ros2_ws/install/setup.bash
ros2 launch waypoint_tools send.launch.py \
  send_waypoint_path:=~/ros2_ws/src/waypoint_tools/config/waypoints/service_test.yaml
```

RViz の Waypoint Sender パネルで `Start` を押します。最初の点に到着して
`SUCCEEDED` になると受信側の定期ログが始まります。送信側はこの点で停止するので
`Next WP` を押します。二つ目の点に到着して定期ログが止まれば、到着からの
開始・停止が確認できています。サービス名は YAML と受信側の `service_name` で
完全に一致させてください。

| パラメータ | 意味 |
|---|---|
| `send_waypoint_path` | 送信する YAML ファイル |
| `switch_radius` [m] | 通過点で次の点へ切り替える距離。Nav2 の goal_checker の `xy_goal_tolerance` より大きくする |
| `max_retries` | 失敗した点を再送する回数 |
| `skip_on_failure` | 再送しても失敗したら次の点へ進むか（false なら停止して `Next WP` 待ち） |
| `robot_frame` | 距離判定に使うロボットの TF フレーム |

Nav2 を起動した後に起動します。起動しただけでは走行せず、RViz パネルで開始します。

```bash
ros2 launch waypoint_tools send.launch.py
```

### RViz パネル（Waypoint Sender）

`Panels` → `Add New Panel` → `waypoint_tools/WaypointSenderPanel` で追加します。

| ボタン | 動作 |
|---|---|
| `Start` | 最初の点から走行開始 |
| `Next WP` | 次の点へ進む |
| `Edit` | 編集モードの切り替え |
| `WP` 欄 + `Start from` | 指定した番号の点から走行開始。番号は RViz 上のラベルと同じ 0 始まり |

### 走行中・走行前の編集

`Edit` を押すと、「2. 編集」と同じ操作で編集できます。

- 走行中に編集モードにすると、その場で止まります。`Next WP` で向かっていた点から再開します。
- 編集はすぐ走行に反映されます。`save` は YAML への保存だけです。
- `Edit` 中も同じ `task` と `action` のメニューを利用できます。
