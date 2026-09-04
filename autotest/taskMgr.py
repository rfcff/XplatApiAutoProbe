# encoding=UTF-8
"""IDE / 本地调试入口。

本仓库未附带业务用例（tests/ 下目前只有 conftest.py）。
请先编写 pytest 用例，再把下方 CASE 改成真实路径后运行；
或直接用 pytest 命令指定用例（见 README / autotest/README.md）。
"""
from __future__ import print_function

import os
import sys

# 改成你仓库内真实存在的用例节点，例如：
#   "tests/p0/test_your_case.py::TestMultiCase::test_Foo"
CASE = ""


def _usage_and_exit():
    print(
        "autotest/taskMgr.py: 未配置可运行的用例。\n"
        "\n"
        "本目录未附带业务用例（tests/ 仅有 conftest.py）。请任选其一：\n"
        "  1) 编写 pytest 用例后，把本文件顶部的 CASE 改成真实节点路径再运行\n"
        "  2) 直接执行：\n"
        "       pytest -s tests/<your_case>.py --devices=<did> --html=report.html\n"
        "\n"
        "详见 ../README.md「autotest」与本目录 README.md「如何运行」。",
        file=sys.stderr,
    )
    sys.exit(2)


if __name__ == "__main__":
    if not CASE:
        _usage_and_exit()

    # 相对本文件所在目录解析，避免 cwd 不在 autotest/ 时找不到用例
    case_path = CASE.split("::", 1)[0]
    abs_case = os.path.join(os.path.dirname(os.path.abspath(__file__)), case_path)
    if not os.path.isfile(abs_case):
        print(
            "autotest/taskMgr.py: CASE 指向的文件不存在: %s\n"
            "请把 CASE 改成真实用例路径后再运行。" % abs_case,
            file=sys.stderr,
        )
        sys.exit(2)

    import pytest  # 仅在确有用例可跑时再依赖 pytest

    pytest.main(["-s", CASE])
