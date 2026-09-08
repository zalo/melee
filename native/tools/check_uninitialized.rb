#!/usr/bin/env ruby
# Compiler diagnostics for every native game/HSD translation unit.
# Reports require semantic inspection; do not blindly initialize every warning.
require 'json'
require 'shellwords'
require 'open3'
Dir.chdir(File.expand_path('../..', __dir__))
entries=JSON.parse(File.read('build/native/compile_commands.json')).select { |e| e['command'].include?('melee_game.dir') }
queue=Queue.new
entries.each_with_index { |e,i| queue << [e,i] }
results=Array.new(entries.size)
8.times.map do
 Thread.new do
  loop do
   begin
    e,i=queue.pop(true)
   rescue ThreadError
    break
   end
   args=Shellwords.split(e['command']); j=args.index('-o');args.slice!(j,2);args.delete('-c')
   args += ['-fsyntax-only','-Wuninitialized','-Wno-return-type']
   text,status=Open3.capture2e(*args,chdir:e['directory'])
   results[i]=text if text.include?('uninitialized') || !status.success?
  end
 end
end.each(&:join)
File.write('build/native-uninitialized.log', results.compact.join("\n"))
puts "Checked #{entries.size} source files; #{results.compact.size} files with diagnostics"
