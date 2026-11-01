"""Write the portable Windows release launcher and its UTF-8 user guide."""

from pathlib import Path


# Keep CMD itself ASCII so its diagnostics do not depend on the console code page.
# A batch file waits for a directly invoked GUI application before the next line.
LAUNCHER = r'''@echo off
setlocal EnableExtensions DisableDelayedExpansion
pushd "%~dp0" >nul 2>&1
if errorlevel 1 goto cannot_open_folder
if not exist "luma_studio.exe" goto missing_executable

".\luma_studio.exe"
set "LUMA_RELEASE_EXIT=%ERRORLEVEL%"
popd
if "%LUMA_RELEASE_EXIT%"=="0" exit /b 0
echo ERROR: LumaLive exited with code %LUMA_RELEASE_EXIT%.
echo See README.md in the release folder for startup requirements.
goto show_error

:missing_executable
echo ERROR: luma_studio.exe was not found next to Start-LumaLive.cmd.
echo Extract the complete release package before starting LumaLive.
popd
set "LUMA_RELEASE_EXIT=2"
goto show_error

:cannot_open_folder
echo ERROR: The release folder could not be opened.
echo Extract the complete package to an accessible folder and try again.
set "LUMA_RELEASE_EXIT=3"

:show_error
if defined LUMALIVE_START_NONINTERACTIVE exit /b %LUMA_RELEASE_EXIT%
if defined CI exit /b %LUMA_RELEASE_EXIT%
pause
exit /b %LUMA_RELEASE_EXIT%
'''


README = '''# LumaLive Windows 开发版使用说明

本包是供验证使用的开发版客户端，不代表全功能完成或生产发布验收通过。
打包只是整理文件，不代表重新编译，也不证明登录、通话、设备或外部 AI 服务已测试通过。
实际构建来源、校验值和验收结果请查看 manifest.json 与本次附带的 build-verification.json；后者不存在时，不能推断已通过运行验收。

## 启动

1. 保留完整发布目录。若收到的是压缩包，请完整解压到可访问的文件夹，不要直接从压缩包内运行。
2. 保持 `Start-LumaLive.cmd` 与 `luma_studio.exe` 在同一目录，保留其余资源与子目录。
3. 双击 `Start-LumaLive.cmd`。启动器会把工作目录切换到包所在目录，然后运行该目录内的客户端。

启动器的路径均使用引号，可用于包含空格或中文的目录。启动器不要求管理员权限，
不安装系统服务，不修改系统配置，也不写入凭据或绕过登录认证。
它不启动账户服务。客户端运行期间，启动窗口会保持打开；请先正常退出客户端，
再关闭启动窗口。启动器不转发额外命令行参数。

若提示 `luma_studio.exe was not found`，请确认完整解压，且未单独移动启动脚本。
若提示无法打开目录，请将完整包解压到当前用户可访问的目录。
客户端异常退出时，窗口会显示退出码并等待按键。启动器返回客户端退出码；
缺少客户端返回 2，无法进入包目录返回 3。
自动化检查可预先设置 `LUMALIVE_START_NONINTERACTIVE=1` 以取消错误后的按键等待。

## 包内依赖

本包包含本地 AI 服务使用的以下文件，使用者无需另外安装 Python：

- `ai-service/gateway.py`
- `ai-service/provider_settings.py`
- `ai-service/runtime/python.exe` 及其配套的 Python 3.14.8 嵌入式运行时文件

请保留完整的 `ai-service` 目录，单独复制 `python.exe` 不足以运行 AI 服务。
客户端按自身可执行文件的位置寻找这些资源，不依赖系统 PATH 中的 Python。
项目 CMake 使用静态 MSVC 运行库；具体发布文件的依赖检查仍以本次测试记录为准。

## 登录与 AI 使用条件

账户服务、数据库及部署证书不包含在此客户端包中。登录需要可访问且配置正确的账户服务，
以及该服务中的有效账户；仅解压客户端不能提供独立的注册或登录后台。
服务不可用时，请联系部署负责人核对服务地址与证书，不要关闭认证或绕过证书检查。
本启动器会继承当前环境及客户端既有配置，不创建或覆盖连接配置。

AI 功能还需要在客户端中配置服务地址、访问密钥、语音识别模型和摘要与翻译模型。
如需朗读，朗读模型和音色需要同时配置。请使用所选提供商支持的模型，并确认网络可达、
凭据有效、账户额度充足以及提供商允许相应请求。包内不附带模型、外部服务账户或访问密钥。
本地服务启动或健康检查成功，只能说明本地状态，不等于真实提供商连接或模型能力已经验收。
本包的外部提供商能力尚未验收。

客户端保存 AI 配置时，会按当前 Windows 用户加密保存；这属于使用者主动执行的应用操作，
启动器不会替使用者写入或修改凭据。请勿在截图、反馈或日志中公开访问密钥。
启用涉及语音的 AI 功能前，应取得参与者同意；音频或文本会发送给所配置的提供商。

## 安全退出

先结束正在进行的通话、录制或 AI 请求，再通过客户端的关闭操作退出，等待进程结束。
客户端自行启动并拥有的本地 AI 子进程由其生命周期管理；已存在并被复用的外部 AI 服务
不由启动器停止。不要批量结束所有 Python 进程，以免影响其他程序。
独立运行的账户服务也不由此启动器停止，需要由部署负责人按其服务流程管理。
若客户端没有响应，请记录本次操作与错误提示后再联系维护者处理。

## 当前验证边界

启动文件生成测试仅验证文件内容、编码和输出规则，不证明客户端在此机器上运行成功。
中文或空格路径下的实际启动、登录、音视频设备、AI 提供商联通和正常退出，
都需要单独的运行验收记录；没有相应记录的项目仍视为未验证。
'''


def _is_link(path: Path) -> bool:
    return path.is_symlink() or getattr(path, "is_junction", lambda: False)()


def write_launchers(destination: Path) -> list[str]:
    """Create launch files without replacing existing files; return relative names.

    The caller owns the staging directory and records release-specific evidence.
    This function neither reads executable metadata nor launches any program.
    """
    destination = Path(destination)
    if _is_link(destination):
        raise ValueError("Release destination must not be a directory link.")
    destination.mkdir(parents=True, exist_ok=True)
    contents = {
        "Start-LumaLive.cmd": LAUNCHER.replace("\n", "\r\n").encode("ascii"),
        "README.md": README.encode("utf-8"),
    }
    # Check both names first so a pre-existing README cannot cause a half-write.
    for name in contents:
        target = destination / name
        if target.exists() or _is_link(target):
            raise FileExistsError(f"Refusing to replace existing release file: {target}")
    for name, data in contents.items():
        with (destination / name).open("xb") as output:
            output.write(data)
    return list(contents)


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    for relative_name in write_launchers(args.destination):
        print(relative_name)
