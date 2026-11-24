# 泰山派人脸门禁

这是基于 RK3566、OV8858、Qt Widgets 和 SG90 的门禁演示。支持扫码和密码开门、用户录入与删除、修改密码，以及通过验证后 3 秒自动回锁。

## 五层结构

| 层 | 入口 | 职责 |
| --- | --- | --- |
| UI 层 | [code/ui/main.cpp](code/ui/main.cpp)、[code/ui/pin_dialog.cpp](code/ui/pin_dialog.cpp)、[code/ui/user_panel.cpp](code/ui/user_panel.cpp) | Qt 界面、触屏键盘和用户列表。调用应用层接口，显示结果。 |
| 应用层 | [code/app/access_app.h](code/app/access_app.h)、[code/app/access_app.cpp](code/app/access_app.cpp) | 协调取帧、录入、扫码和密码开门。负责相似度阈值与放行判定。 |
| 服务层 | [code/services/](code/services/) | 人脸处理、名单管理、密码管理和开锁后定时回锁。 |
| 平台层 | [code/platform/](code/platform/) | Linux 子进程、文件、进程锁、BMP 编码和 OpenSSL。保留原有数据格式。 |
| 驱动层 | [code/drivers/camera.cpp](code/drivers/camera.cpp)、[code/drivers/servo_lock.cpp](code/drivers/servo_lock.cpp) | 通过 V4L2 采集 OV8858 画面，通过 PWM8 控制 SG90。 |

依赖方向是 UI → 应用 → 服务。应用和服务调用驱动或平台，Qt 类型只在 **code/ui/** 中出现。

```text
点击「扫脸开门」
  → AccessApp::scanFrame()
    → FaceService 提取特征
    → 与 UserStore 中的名单比对，按阈值判定
    → DoorService::unlock()
      → ServoLock 向 PWM8 发送开位脉宽
      → DoorService 计时 3 秒后发送回锁脉宽
```

相机驱动返回只读 RGB 帧。Qt 的 VideoView 在绘制时用 QImage 包装这些像素。人脸和密码按钮的后台任务只调用 AccessApp，关锁计时由服务层独立完成。

## 编译和运行

在 Ubuntu 的项目根目录运行：

```sh
bash tools/build_access.sh
```

qmake 工程在 [code/ui/access_preview.pro](code/ui/access_preview.pro)，编译和打包脚本在 **tools/**。发布包在 **build/qt_preview_package/**，板上启动脚本仍叫 **run.sh**。可用 **bash tools/check_core.sh** 在有 GCC 9+ 和 OpenSSL 开发头文件的 Linux 上运行核心检查。不需要 Qt、摄像头或舵机。

板上空载验证联动：

```sh
cd /userdata/access_preview
./run.sh --threshold 0.50 --servo-lock-us 1500 --servo-open-us 2100
```

两处脉宽用于空载调试。装连杆前按门闩实际行程重新标定。未传舵机参数时可以测试开门判定。阈值 0.50 为演示值。

## 数据与资源

- 用户名单 **/userdata/access_preview/users.json**。最多 10 位用户，录入时使用最小空号。首次创建名单时导入旧特征，删除后保存空名单，重启不会重新导入。
- 密码校验文件 **/userdata/access_preview/pin.json**。三个密码入口共用同一份加盐 PBKDF2-SHA256 记录，只有首次创建时初始密码才是 123456。
- 板上识别工具和模型位于 **/userdata/detect_align_package**。其命令行工具由 **detect_align/** 构建。数据和模型分别在 **data/**、**models/**。
- **third_party/cjson/** 来自泰山派 SDK 的 cJSON（MIT 许可），解析已有 JSON 数据。RetinaFace 源码保存在 **third_party/retinaface/**。
