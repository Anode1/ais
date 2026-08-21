# ais_engine.podspec -- the C engine, compiled into the iOS app binary.
#
# iOS does not load a .so: lib/ais_ffi.dart resolves the engine with
# DynamicLibrary.process(), so the symbols have to be part of the app itself.
# This pod is the plain-text equivalent of adding the sources to the Runner
# target by hand in Xcode, and it keeps the file list in a file rather than in
# Xcode project state, where a diff cannot see it.
#
# It lives at the repo ROOT because a CocoaPods file pattern may not climb out
# of the pod's own directory, and the engine is `c/`. app/flutter/ios/Podfile
# refers to it as `pod 'ais_engine', :path => '../../..'`.
#
# The file list mirrors app/flutter/src/CMakeLists.txt, the build Android and
# Linux use: every c/*.c and c/crypto/*.c, minus the two that carry a main().
Pod::Spec.new do |s|
  s.name     = 'ais_engine'
  s.version  = '0.0.1'          # a pod version CocoaPods needs; the ENGINE's
                                # version is AIS_VERSION, stamped from the git
                                # tag (see doc/dev/VERSIONING.md)
  s.summary  = 'The AIS index engine (C99), linked into the iOS app.'
  s.homepage = 'https://github.com/Anode1/ais'
  s.license  = { :type => 'GPL-3.0', :file => 'COPYING' }
  s.author   = { 'AIS' => 'https://github.com/Anode1/ais' }
  s.source   = { :path => '.' }
  s.platform = :ios, '13.0'

  s.requires_arc = false        # C, not Objective-C

  s.source_files  = 'c/*.{c,h}', 'c/crypto/*.{c,h}'
  # Three files stay out, and the list differs from CMakeLists by the third:
  #   main.c   the CLI's entry point, and tests.c the test suite's. Each carries
  #            a main(), which collides with the app's own.
  #   serve.c  the web GUI's HTTP server, whose only caller is the CLI
  #            (ais_serve, from main.c). It cannot compile for iOS anyway:
  #            system() is unavailable there, and it uses it to open a desktop
  #            browser at the served page. Nothing in the app reaches it, so
  #            this drops a file the app never had a use for rather than
  #            teaching the engine about iOS.
  s.exclude_files = 'c/main.c', 'c/tests.c', 'c/serve.c'

  s.pod_target_xcconfig = {
    # The engine includes its own headers relatively ("common.h",
    # "crypto/monocypher.h"), so c/ itself has to be on the search path.
    'HEADER_SEARCH_PATHS'        => '"$(PODS_TARGET_SRCROOT)/c"',
    'GCC_C_LANGUAGE_STANDARD'    => 'c99',
    # DynamicLibrary.process() looks every symbol up by NAME at runtime, so
    # nothing may be hidden at link time.
    'GCC_SYMBOLS_PRIVATE_EXTERN' => 'NO',
  }
end
