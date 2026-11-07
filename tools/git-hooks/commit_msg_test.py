"""
@file  commit_msg_test.py
@brief 提交信息中理想化子项编号的回归检查

校验清单允许 10a、12c 等编号，脚注沿用相同编号。
@see CONTRIBUTING.md
"""

from pathlib import Path
import runpy
import unittest


HOOK = runpy.run_path(str(Path(__file__).with_name("commit-msg")))


class IdealizationTest(unittest.TestCase):
    """保留数字条目，并接受规范中的小写字母子项。"""

    def test_accepts_numbers_and_subitems(self):
        """单项和多项脚注使用同一套编号规则。"""
        for value in ("add #25", "update #10a", "remove #12c",
                      "update #9, update #10a, add #26"):
            with self.subTest(value=value):
                self.assertIsNotNone(HOOK["IDEALIZATION_RE"].fullmatch(value))

    def test_rejects_malformed_entries(self):
        """子项支持保留操作名、空格及单字母后缀约束。"""
        for value in ("update #10A", "update #10ab", "update #10-",
                      "update#10a", "change #10a", "add #25,update #10a"):
            with self.subTest(value=value):
                self.assertIsNone(HOOK["IDEALIZATION_RE"].fullmatch(value))

    def test_validates_complete_message(self):
        """完整提交信息按相同规则接受理想化子项。"""
        message = ["fix(git): 支持理想化子项编号", "",
                   "Idealization: update #10a, update #12c"]
        self.assertEqual(HOOK["check"](message, "feat/test"), [])


if __name__ == "__main__":
    unittest.main()
