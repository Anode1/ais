# ais_engine.podspec -- the C engine, built into the iOS app.
#
# lib/ais_ffi.dart resolves the engine with DynamicLibrary.process(), which
# searches the images already loaded in the process, so the engine has to arrive
# with the app rather than as a file opened by path. Under Flutter's
# use_frameworks! it builds as ais_engine.framework inside the app bundle,
# linked at launch; CI asserts ais_embed_open is really in there, because a
# build that dropped it would fail on the first engine call instead of here.
#
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

  # Stamp the engine's version from the git tag, as c/Makefile does for the CLI
  # and CMakeLists.txt for Android and Linux. Without it ais_version() reports
  # ais.h's "0.0.0-dev" fallback, and the About screen -- which exists so a bug
  # report names the engine it was filed against -- cannot do its job. A source
  # copy with no git keeps the fallback, which is visibly wrong rather than
  # silently wrong. A release is built at a tag, so ask for the tag first;
  # off a tag, the descriptive form, dirty marker included.
  git_dir = File.expand_path(__dir__)
  ais_version = `git -C "#{git_dir}" describe --exact-match --tags HEAD 2>/dev/null`.strip
  ais_version = `git -C "#{git_dir}" describe --tags --always --dirty 2>/dev/null`.strip if ais_version.empty?
  ais_version = ais_version.sub(/\Av/, '')

  s.pod_target_xcconfig = {
    # The engine includes its own headers relatively ("common.h",
    # "crypto/monocypher.h"), so c/ itself has to be on the search path.
    'HEADER_SEARCH_PATHS'        => '"$(PODS_TARGET_SRCROOT)/c"',
    'GCC_C_LANGUAGE_STANDARD'    => 'c99',
    # DynamicLibrary.process() looks every symbol up by NAME at runtime, so
    # nothing may be hidden at link time.
    'GCC_SYMBOLS_PRIVATE_EXTERN' => 'NO',
  }.merge(
    ais_version.empty? ? {} :
      { 'GCC_PREPROCESSOR_DEFINITIONS' => "$(inherited) AIS_VERSION=\\\"#{ais_version}\\\"" }
  )
end
