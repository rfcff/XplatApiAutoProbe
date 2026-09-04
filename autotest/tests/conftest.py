from datetime import datetime

import pytest
import pytest_html
from py.xml import html
from pytz import timezone

from multiTest.multiCaseRun import PASS
from runCaseHelper import tearDown
import sys, os

sys.path.append((os.path.abspath(os.path.join(os.path.dirname(__file__), './'))))

'''
pytest配置文件,跑用例会自动跑到这里
'''

startTime = 0  # 用例开始时间


@pytest.hookimpl(hookwrapper=True, tryfirst=True)
def pytest_runtest_makereport(item, call):
    # 获取钩子方法的调用结果
    result = yield

    # 从钩子方法的调用结果中获取测试报告
    report = result.get_result()
    # 当前用例名
    case_name = report.nodeid.split('::')[-1]

    if report.when == 'setup':
        global startTime
        cst_tz = timezone('Asia/Shanghai')
        now = datetime.now().replace(tzinfo=cst_tz)
        startTime = now.strftime('%Y-%m-%d %H:%M:%S')

    caseResult = report.outcome if report.outcome == 'failed' else PASS
    extra = getattr(report, 'extra', [])
    if report.when == 'call':
        report.startTime = startTime
        report.description = str(item.function.__doc__)
        report.nodeid = report.nodeid.encode("utf-8").decode("unicode_escape")  # 设置编码显示中文
        logs = tearDown(caseResult)

        for log in logs:
            extra.append(pytest_html.extras.url(log))
        xfail = hasattr(report, 'wasxfail')
        if (report.skipped and xfail) or (report.failed and not xfail):
            # only add additional html on failure
            extra.append(pytest_html.extras.html('<div>Additional HTML</div>'))
        report.extra = extra


def pytest_addoption(parser):
    parser.addoption("-U", "--appid", action="store", default="2003426465", help="appid")
    parser.addoption("--uid1", action="store", default="217118", help="userA uid")
    parser.addoption("--uid2", action="store", default="217119", help="userB uid")
    parser.addoption("--uid3", action="store", default="217120", help="userC uid")
    parser.addoption("--channel1", action="store", default="65935", help="channel1 id")
    parser.addoption("--channel2", action="store", default="73706", help="channel2 id")
    parser.addoption("--devices", action="store", default="", help="deviceId，格式 did1,did2,did3")


appid = None
uid1 = None
uid2 = None
uid3 = None
channel1 = None
channel2 = None
devicesFromCloud = None


def pytest_configure(config):
    global appid
    global uid1
    global uid2
    global uid3
    global channel1
    global channel2
    global devicesFromCloud
    appid = config.getoption("--appid")
    uid1 = config.getoption("--uid1")
    uid2 = config.getoption("--uid2")
    uid3 = config.getoption("--uid3")
    channel1 = config.getoption("--channel1")
    channel2 = config.getoption("--channel2")
    devicesFromCloud = config.getoption("--devices")


@pytest.hookimpl(optionalhook=True)
def pytest_html_results_table_header(cells):
    cells.insert(1, html.th('Start Time'))
    cells.insert(2, html.th('Description'))


@pytest.hookimpl(optionalhook=True)
def pytest_html_results_table_row(report, cells):
    cells.insert(1, html.td(report.startTime))
    cells.insert(2, html.td(report.description))
