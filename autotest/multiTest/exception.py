# encoding=UTF-8


class BaseException(Exception):
    def __init__(self, deviceId = '', msg=''):
        Exception.__init__(self, msg)
        self.deviceId = deviceId
        self.exceptionMsg = msg

    def __str__(self):
        return repr('[%s] %s' % (self.deviceId, self.exceptionMsg))


class TestFailNotify(BaseException):
    def __init__(self, deviceId = '', msg=''):
        BaseException.__init__(self, deviceId, msg)


class NetworkException(BaseException):
     def __init__(self, deviceId = '', msg= ''):
        BaseException.__init__(self, deviceId, msg)

class TimeoutException(BaseException):
    def __init__(self, deviceId = '', msg = ''):
        BaseException.__init__(self, deviceId, msg)


class HTTPServerError(BaseException):
     def __init__(self,deviceId='', msg=''):
        BaseException.__init__(self,deviceId,msg)


class ValueChangeError(BaseException):
     def __init__(self,deviceId='', msg=''):
        BaseException.__init__(self,deviceId,msg)


class ResException(BaseException):
     def __init__(self,deviceId='', msg=''):
        BaseException.__init__(self,deviceId,msg)


class VerifyException(BaseException):
     def __init__(self,deviceId='', msg=''):
        BaseException.__init__(self,deviceId,msg)


class AppTimeOut(BaseException):
    def __init__(self,deviceId='', msg=''):
        BaseException.__init__(self, deviceId, msg)


class AppCrash(BaseException):
    def __init__(self,deviceId='', msg=''):
        BaseException.__init__(self, deviceId, msg)