# esp-compute

ESP32 项目的**云端算力仓库**。所有吃 CPU / 内存的活儿（编译固件、跑构建、批量计算、压测）都在 GitHub Actions 上执行，**不占用本地电脑资源**。

## 为什么建这个仓库

- 公开仓库的 GitHub Actions **分钟数无限免费**（私有仓库每月只有 2000 分钟，超出要付费）
- 算力是 GitHub 的服务器，不是你自己的电脑
- 单次任务最长可跑 6 小时；ubuntu 机器 4 核 / 16GB 内存 / 14GB SSD

## 怎么用

### 方式一：网页点按钮（最省事）

1. 打开 [Actions](../../actions) 页面
2. 左侧选 **compute**
3. 右侧点 **Run workflow**
4. 填参数：
   - `script` —— 要跑的 shell 脚本
   - `runner` —— 运行环境，默认 `ubuntu-latest`
   - `job_name` —— 任务名，会用作产物名
   - `timeout_minutes` —— 超时分钟数，默认 60
5. 点绿色 **Run workflow**，跑完后在任务页底部下载 `output-<job_name>` 产物

### 方式二：命令行触发

```bash
curl -X POST \
  -H "Authorization: Bearer $GITHUB_TOKEN" \
  -H "Accept: application/vnd.github+json" \
  https://api.github.com/repos/kmm-plf/esp-compute/actions/workflows/compute.yml/dispatches \
  -d '{"ref":"main","inputs":{"job_name":"test","script":"make -j$(nproc)"}}'
```

## 运行环境规格

| runner | 规格 | 计费倍数 |
|---|---|---|
| ubuntu-latest | 4 核 / 16GB / 14GB SSD | 1x |
| windows-latest | 4 核 / 16GB / 14GB SSD | 2x |
| macos-latest | 3 核 / 7GB / 14GB SSD | 10x |

公开仓库全部免费，倍数不影响费用。

## 产物

- `output.log` —— 脚本的完整 stdout + stderr
- `out/` —— 脚本里凡是写进仓库根目录 `out/` 的文件，都会被一起打包上传

默认保留 7 天。

## 注意

- 运行环境是 **UTC 时区**，要北京时间请自行 `TZ=Asia/Shanghai`
- **这个仓库是公开的**，不要把密钥、令牌、私密数据写进脚本或提交上来
- GitHub Actions 的条款要求任务与该仓库的软件项目相关，别拿它跑无关的挖矿之类的东西，会封号