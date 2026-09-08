#!/usr/bin/env ruby
# Run a real native game session with reproducible PAD input and a wall-clock
# deadline. Completing the sequence proves input/scene progress, not visual fidelity.
require 'fileutils'

root = File.expand_path('../..', __dir__)
Dir.chdir(root)
linux = RUBY_PLATFORM.include?('linux')
script = File.expand_path(ARGV.fetch(0, 'native/tests/match-controls.input'))
deadline_seconds = Integer(ARGV.fetch(1, '180'))
disc = File.expand_path(ARGV.fetch(2, ENV.fetch('MELEE_TEST_DISC', linux ? 'build/disc/melee-us-1.02.ciso' : 'build/disc/Super Smash Bros. Melee (USA) (En,Ja) (Rev 2).ciso')))
raise 'Expected positive timeout' unless deadline_seconds.positive?
raise 'Input script or disc missing' unless File.file?(script) && File.file?(disc)
video_seconds = Integer(ENV.fetch('MELEE_TEST_VIDEO_SECONDS', '0'))
raise 'Video duration must be 0-60 seconds' unless (0..60).cover?(video_seconds)
record_source = 'native/tools/record_game.swift'
record_tool = 'build/native-record-game'
if video_seconds.positive? && (!File.exist?(record_tool) || File.mtime(record_tool) < File.mtime(record_source))
  raise 'Could not build game window recorder' unless system('swiftc', '-parse-as-library', record_source, '-o', record_tool)
end
capture_source = 'native/tools/capture_game.swift'
capture_tool = 'build/native-capture-game'
capture_enabled = ENV.fetch('MELEE_TEST_CAPTURE', linux ? '0' : '1') == '1'
raise 'Linux runner supports numeric render checks; disable Mac capture/video options' if linux && (capture_enabled || video_seconds.positive?)
if capture_enabled && (!File.exist?(capture_tool) || File.mtime(capture_tool) < File.mtime(capture_source))
  raise 'Could not build game window capture tool' unless system('swiftc', capture_source, '-o', capture_tool)
end
directory = File.join(root, 'build/native-runs', Time.now.strftime('%Y%m%d-%H%M%S'))
FileUtils.mkdir_p(directory)
input = File.join(directory, 'input.txt')
log_path = File.join(directory, 'game.log')
FileUtils.cp(script, input)
test_seed = Integer(ENV.fetch('MELEE_TEST_SEED', '1'))
raise 'Test seed must fit uint32' unless (0..0xFFFFFFFF).cover?(test_seed)
File.write(File.join(directory, 'seed.txt'), "#{test_seed}\n")
environment = {
  'MELEE_TEST_SEED' => test_seed.to_s,
  'MELEE_INPUT_SCRIPT' => input, 'MELEE_TRACE_ASSETS' => '1',
  'ASAN_OPTIONS' => 'detect_leaks=0:color=never',
  'UBSAN_OPTIONS' => 'print_stacktrace=1:halt_on_error=1'
}
if linux && ENV['MELEE_TEST_CLEAN_LIBRARY_ENV'] == '1'
  environment['LD_LIBRARY_PATH'] = nil
  environment['LD_PRELOAD'] = nil
end
if ENV['MELEE_TEST_CRASH_TRACE'] == '1'
  trace_source = 'native/tools/crash_trace.c'
  trace_library = File.join(root, linux ? 'build/native-crash-trace.so' : 'build/native-crash-trace.dylib')
  if !File.file?(trace_library) || File.mtime(trace_library) < File.mtime(trace_source)
    flags = linux ? ['-shared', '-fPIC'] : ['-dynamiclib']
    raise 'Could not build crash tracer' unless system('cc', *flags, '-g', trace_source, '-o', trace_library)
  end
  preload = linux ? 'LD_PRELOAD' : 'DYLD_INSERT_LIBRARIES'
  environment[preload] = [ENV[preload], trace_library].compact.join(':')
end
executable = File.expand_path(ENV.fetch('MELEE_TEST_APP', linux ? 'build/native-linux/melee_native' : 'build/native/melee_mac.app/Contents/MacOS/melee_mac'))
run_directory = File.expand_path(ENV.fetch('MELEE_TEST_CWD', root))
pid = Process.spawn(environment, executable, disc, chdir: run_directory,
                    out: log_path, err: [:child, :out])
File.write(File.join(directory, 'pid'), "#{pid}\n")
puts "Native input test PID #{pid}; artifacts: #{directory}"
$stdout.flush
deadline = Process.clock_gettime(Process::CLOCK_MONOTONIC) + deadline_seconds
result = nil
scene_seen_at = {}
captured = {}
video_pid = nil
begin
  loop do
    begin
      ended = Process.waitpid2(pid, Process::WNOHANG)
    rescue Errno::ECHILD
      result = 'FAIL: child was reaped externally (for example by the debugger); inspect game.log'
      pid = nil
      break
    end
    if ended
      result = "FAIL: game exited before test completion (#{ended[1]})"
      pid = nil
      break
    end
    log = File.read(log_path)
    now = Process.clock_gettime(Process::CLOCK_MONOTONIC)
    if video_seconds.positive? && !video_pid && log.include?("[input-test] ready scene 2\n")
      video_pid = Process.spawn(record_tool, pid.to_s, File.join(directory, 'match.mp4'), video_seconds.to_s,
                                out: File.join(directory, 'video-capture.log'), err: [:child, :out])
    end
    (capture_enabled ? {2 => ['match', 6], 5 => ['results', 5]} : {}).each do |scene, (name, delay)|
      scene_seen_at[scene] ||= now if log.include?("[input-test] ready scene #{scene}\n")
      if scene_seen_at[scene] && !captured[scene] && now - scene_seen_at[scene] >= delay
        captured[scene] = true
        system(capture_tool, pid.to_s, File.join(directory, "#{name}.png"),
               out: File.join(directory, "#{name}-capture.log"), err: [:child, :out])
      end
    end
    if log.include?('[input-test] script finished; manual control restored')
      # Let a crashing process finish symbolizing its report before reaping it.
      # Killing at the first ASan line discards the useful stack trace.
      result = log.match?(/AddressSanitizer:|runtime error:|\[input-test\] timed out/) ?
        'FAIL: sanitizer or scene timeout; inspect game.log' :
        'PASS: input sequence completed; inspect render metrics for playfield coverage'
      break
    end
    if Process.clock_gettime(Process::CLOCK_MONOTONIC) >= deadline
      result = 'FAIL: wall-clock timeout; inspect game.log'
      system('/usr/bin/sample', pid.to_s, '1', '10', '-file', File.join(directory, 'threads.txt'),
             out: File.join(directory, 'sample.log'), err: [:child, :out]) unless linux
      break
    end
    sleep 0.5
  end
ensure
  if video_pid
    _, video_status = Process.waitpid2(video_pid)
    result = "FAIL: video capture failed (#{video_status}); inspect video-capture.log" unless video_status.success?
  end
  if pid
    Process.kill('TERM', pid) rescue Errno::ESRCH
    reaped = false
    20.times do
      begin
        reaped = !!Process.waitpid(pid, Process::WNOHANG)
      rescue Errno::ECHILD
        reaped = true
      end
      break if reaped
      sleep 0.1
    end
    unless reaped
      Process.kill('KILL', pid) rescue Errno::ESRCH
      Process.waitpid(pid) rescue Errno::ECHILD
    end
  end
end
File.write(File.join(directory, 'result.txt'), "#{result}\n")
puts result
exit(result&.start_with?('PASS:') ? 0 : 1)
