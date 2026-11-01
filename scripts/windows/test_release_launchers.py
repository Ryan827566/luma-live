"""Generation checks only; actual CMD/client execution is a separate QA step."""

from pathlib import Path
import shutil
import tempfile
import unittest
import uuid

from release_launchers import write_launchers


class ReleaseLauncherTests(unittest.TestCase):
    def setUp(self):
        # Default mkdir inherits the Windows sandbox ACL. TemporaryDirectory's
        # owner-only ACL on Python 3.14 can exclude the sandbox token itself.
        self.temp_parent = Path(tempfile.gettempdir()).resolve()
        self.root = self.temp_parent / ("luma-launchers-" + uuid.uuid4().hex)
        self.root.mkdir()
        self.addCleanup(self.clean_temporary_directory)
        self.destination = self.root / "发布包 中文 and spaces !"

    def clean_temporary_directory(self):
        resolved = self.root.resolve()
        if resolved.parent != self.temp_parent or not resolved.name.startswith("luma-launchers-"):
            raise AssertionError("Refusing to clean an unexpected test directory.")
        shutil.rmtree(resolved)

    def test_generates_only_declared_files_in_unicode_directory(self):
        names = write_launchers(self.destination)
        self.assertEqual(names, ["Start-LumaLive.cmd", "README.md"])
        self.assertEqual(set(names), {p.name for p in self.destination.iterdir()})
        self.assertTrue(all(not Path(name).is_absolute() for name in names))

    def test_cmd_has_crlf_and_pins_cwd_and_executable(self):
        write_launchers(self.destination)
        data = (self.destination / "Start-LumaLive.cmd").read_bytes()
        text = data.decode("ascii")
        self.assertNotIn(b"\n", data.replace(b"\r\n", b""))
        self.assertIn('setlocal EnableExtensions DisableDelayedExpansion\r\n', text)
        self.assertIn('pushd "%~dp0" >nul 2>&1\r\n', text)
        self.assertIn('".\\luma_studio.exe"\r\nset "LUMA_RELEASE_EXIT=%ERRORLEVEL%"\r\npopd', text)
        self.assertNotIn("%*", text)

    def test_errors_are_readable_and_noninteractive_mode_is_available(self):
        write_launchers(self.destination)
        text = (self.destination / "Start-LumaLive.cmd").read_text(encoding="ascii")
        self.assertIn('if not exist "luma_studio.exe" goto missing_executable', text)
        self.assertIn("luma_studio.exe was not found next to Start-LumaLive.cmd", text)
        self.assertIn('set "LUMA_RELEASE_EXIT=2"', text)
        self.assertIn('set "LUMA_RELEASE_EXIT=3"', text)
        self.assertIn("if defined LUMALIVE_START_NONINTERACTIVE exit /b %LUMA_RELEASE_EXIT%", text)

    def test_readme_is_utf8_and_states_runtime_and_validation_limits(self):
        write_launchers(self.destination)
        text = (self.destination / "README.md").read_bytes().decode("utf-8")
        for expected in ("开发版", "不代表重新编译", "无需另外安装 Python",
                         "ai-service/gateway.py", "ai-service/provider_settings.py",
                         "ai-service/runtime/python.exe", "账户服务、数据库及部署证书不包含",
                         "外部提供商能力尚未验收", "安全退出", "仍视为未验证"):
            self.assertIn(expected, text)

    def test_existing_readme_is_preserved_without_creating_launcher(self):
        self.destination.mkdir(parents=True)
        existing = self.destination / "README.md"
        existing.write_bytes(b"preserve existing material")
        with self.assertRaises(FileExistsError):
            write_launchers(self.destination)
        self.assertEqual(existing.read_bytes(), b"preserve existing material")
        self.assertFalse((self.destination / "Start-LumaLive.cmd").exists())

    def test_other_staging_files_are_untouched(self):
        self.destination.mkdir(parents=True)
        executable = self.destination / "luma_studio.exe"
        executable.write_bytes(b"fixture only: not an executable")
        write_launchers(self.destination)
        self.assertEqual(executable.read_bytes(), b"fixture only: not an executable")
        with self.assertRaises(FileExistsError):
            write_launchers(self.destination)


if __name__ == "__main__":
    unittest.main()
