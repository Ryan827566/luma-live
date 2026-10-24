# 验证报告和线程诊断的独立验收

这是 2026-10-24 两个脚本模块的独立故障验收，不运行客户端 GUI、CMake、账户服务或数据库。Windows 上检查当前 Python 进程的线程元数据；故障用例同时使用临时目录、短 Python 子进程及 API 注入。它不能代替真实客户端 27 项回归、五分钟 soak、物理设备或生产部署验收。

`scripts/windows/test_independent_acceptance.py` 接受文件路径参数。原始基线固定为 `220575809e805b2bde4e209c770a5891a2ea0b07`；比较原版与当前版的 27 项命令及超时、六个负载函数和常量，防止通过删除检查或降低门槛制造通过。将来有意改变这些流程时，应重新审查和更新验收基线，而非绕过失败。

在 Windows PowerShell 执行以下命令；Python 路径为本机已核验路径。无需启动正式服务。报告和两份基线文件保留在独立的 output 子目录中，不应提交到 Git。

```powershell
Set-Location -LiteralPath 'D:\workspace\luma-live'
$runTag = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + ([guid]::NewGuid().ToString('N').Substring(0,8))
$evidenceDir = Join-Path (Get-Location).Path "output/verification-evidence-$runTag"
New-Item -ItemType Directory -Path $evidenceDir -ErrorAction Stop | Out-Null
$baseline = '220575809e805b2bde4e209c770a5891a2ea0b07'
$oldVerifier = git show "${baseline}:scripts/windows/verify_client.py"
if ($LASTEXITCODE -ne 0) { throw 'Cannot read baseline verifier' }
$oldChurn = git show "${baseline}:scripts/windows/test_account_connection_churn.py"
if ($LASTEXITCODE -ne 0) { throw 'Cannot read baseline churn' }
$originalVerifier = Join-Path $evidenceDir 'verify_client.original.py'
$originalChurn = Join-Path $evidenceDir 'test_account_connection_churn.original.py'
[IO.File]::WriteAllLines($originalVerifier, [string[]]$oldVerifier, [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllLines($originalChurn, [string[]]$oldChurn, [Text.UTF8Encoding]::new($false))
$resultPath = Join-Path $evidenceDir 'acceptance.json'
$logPath = Join-Path $evidenceDir 'acceptance.log'
& 'C:\Python314\python.exe' -B scripts/windows/test_independent_acceptance.py `
  --verify scripts/windows/verify_client.py `
  --diagnostics scripts/windows/account_thread_diagnostics.py `
  --original $originalVerifier `
  --churn scripts/windows/test_account_connection_churn.py `
  --original-churn $originalChurn --result $resultPath *> $logPath
$exitCode = $LASTEXITCODE
Get-Content -Raw -LiteralPath $resultPath
$exitCode
```

本版预期 `tests_run=20`、`passed=true`、failures/errors/skipped 均为空，退出码为 0。任何失败、错误、意外跳过或无报告均不算通过。结果包含被测文件 SHA256，便于绑定版本。请保留失败日志；测试会清理它自己创建的临时目录，不需要批量结束进程或清理其他目录。

2026-10-25 已用上述 Git 基线导出方法，在 D 盘仓库内执行版本化脚本，20 项全部通过；实际结果位于 `output/verification-evidence-20261025/acceptance.json`。此前的真实隔离 soak 仍失败，详见同目录日期开发报告。此脚本的通过状态不改变该失败结论。
