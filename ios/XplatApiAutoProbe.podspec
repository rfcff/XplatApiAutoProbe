Pod::Spec.new do |s|
  s.name             = 'XplatApiAutoProbe'
  s.version          = '1.0.0'
  s.summary          = 'Cross-platform API auto-probe RPC server for iOS automated testing.'

  s.description      = <<-DESC
  XplatApiAutoProbe (iOS) is a lightweight in-app TCP RPC server for
  automated API testing. It exposes SDK APIs to test clients over a
  framed JSON protocol (4-byte big-endian length header + UTF-8 JSON
  payload): PING/PONG heartbeat, GET_API method enumeration, ver=2
  custom invocation dispatch, ver=1 NSInvocation reflection calls and
  field setters, with main/background threadMode support. Built with
  CFSocket + RunLoop, zero external dependencies.
                       DESC

  s.homepage         = 'https://github.com/xprobe/XplatApiAutoProbe'
  s.license          = { :type => 'MIT' }
  s.author           = { 'XplatApiAutoProbe Contributors' => 'xprobe@example.com' }
  s.source           = { :git => 'https://github.com/xprobe/XplatApiAutoProbe.git', :tag => s.version.to_s }

  s.ios.deployment_target = '9.0'
  s.osx.deployment_target = '10.11'

  s.source_files        = 'xprobe/**/*.{h,mm}'
  s.public_header_files = [
    'xprobe/XplatApiAutoProbe.h',
    'xprobe/XPTestMgr.h',
    'xprobe/XPLog.h',
    'xprobe/XPReflect.h',
    'xprobe/XPCmdRunner.h',
    'xprobe/XPBaseInstMgr.h',
    'xprobe/XPCustomInvocation.h',
    'xprobe/XPTypeDefine.h',
    'xprobe/connect/XPConnMgr.h',
    'xprobe/connect/XPServerConn.h',
  ]

  s.frameworks    = 'Foundation', 'CoreGraphics'
  s.requires_arc  = true
end
