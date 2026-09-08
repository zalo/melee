#!/usr/bin/env ruby
require 'json'
require 'fileutils'
require 'open3'
require 'digest'

root = File.expand_path('../..', __dir__)
Dir.chdir(root)
stage_names = %w[Dummy Test Fountain-of-Dreams Pokemon-Stadium Princess-Peach-Castle Kongo-Jungle Brinstar Corneria Yoshis-Story Onett Mute-City Rainbow-Cruise Jungle-Japes Great-Bay Hyrule-Temple Brinstar-Depths Yoshis-Island Green-Greens Fourside Mushroom-Kingdom Mushroom-Kingdom-II Akaneia Venom Poke-Floats Big-Blue Icicle-Mountain Icetop Flat-Zone Dream-Land Yoshis-Island-64 Kongo-Jungle-64 Battlefield Final-Destination]
character_names = %w[Captain-Falcon Donkey-Kong Fox Game-and-Watch Kirby Bowser Link Luigi Mario Marth Mewtwo Ness Peach Pikachu Ice-Climbers Jigglypuff Samus Yoshi Zelda Sheik Falco Young-Link Dr-Mario Roy Pichu Ganondorf]
item_names = %w[Capsule Crate Barrel Egg Party-Ball Barrel-Cannon Bob-omb Mr-Saturn Heart-Container Maxim-Tomato Starman Home-Run-Bat Beam-Sword Parasol Green-Shell Red-Shell Ray-Gun Freezie Food Proximity-Mine Flipper Super-Scope Star-Rod Lips-Stick Fan Fire-Flower Super-Mushroom Poison-Mushroom Hammer Warp-Star Screw-Attack Bunny-Hood Metal-Box Cloaking-Device Poke-Ball]
mode = ARGV.fetch(0, 'stages')
ids = ARGV[1]&.split(',')&.map { |id| Integer(id) }
cases = case mode
        when 'stages'
          (ids || (2..32).to_a - [21, 26]).map { |id| {group: mode, id: id, name: stage_names.fetch(id), stage: id, character: 2} }
        when 'characters'
          (ids || (0..25).to_a).map { |id| {group: mode, id: id, name: character_names.fetch(id), stage: 9, character: id} }
        when 'combined'
          # Keep completed stage coverage; remaining stages, characters and
          # common items share one match each. Selection is checked on load.
          completed_stages = ENV.fetch('MELEE_TEST_COMPLETED_STAGES', '').split(',').map { |id| Integer(id) }
          remaining_stages = (2..32).to_a - [21, 26] - completed_stages
          (ids || (0..34).to_a).map do |id|
            stage = remaining_stages[id] || 9
            character = id % 26
            opponent = (character + 13) % 26
            {group: mode, id: id,
             name: "#{stage_names.fetch(stage)} / #{character_names.fetch(character)} vs #{character_names.fetch(opponent)} / #{item_names.fetch(id)}",
             stage: stage, character: character, opponent: opponent, item: id}
          end
        when 'items'

          (ids || (0..34).to_a).map { |id| {group: mode, id: id, name: item_names.fetch(id), stage: 9, character: 2, item: id} }
        else raise 'Expected stages, characters, items, or combined'
        end
directory = File.expand_path("build/native-matrix/#{Time.now.strftime('%Y%m%d-%H%M%S')}-#{mode}")
FileUtils.mkdir_p(directory)
script = File.join(directory, 'combat.input')
input = File.read('native/tests/to-match.input').sub(/180 NONE\s*\z/, "900 NONE\n")
input = input.sub(/# Move the P1 glove.*?600 SCENE_SSS/m, "# Matrix hook preselects the two players.\n6 START\n600 SCENE_SSS")
input = input.sub(/600 SCENE_SSS.*?1800 SCENE_MATCH/m, '1800 SCENE_MATCH')
File.write(script, input)
binary = File.expand_path(ENV.fetch('MELEE_TEST_APP', 'build/native/melee_mac.app/Contents/MacOS/melee_mac'))
report = {binary: binary, sha256: Digest::SHA256.file(binary).hexdigest, coverage: '900-frame match samples including countdown; not exhaustive move or visual acceptance', cases: cases}
save = -> { File.write(File.join(directory, 'report.json'), JSON.pretty_generate(report) + "\n") }
save.call
puts "Matrix report: #{directory}/report.json"
$stdout.flush
cases.each do |entry|
  entry[:sha256] = Digest::SHA256.file(binary).hexdigest
  puts "Starting #{entry[:group]} #{entry[:id]} #{entry[:name]}"
  $stdout.flush
  env = {'MELEE_MATRIX_TEST'=>'1', 'MELEE_TEST_FORCE_STAGE'=>'1', 'MELEE_TEST_STAGE'=>entry[:stage].to_s,
         'MELEE_TEST_CHARACTER'=>entry[:character].to_s, 'MELEE_TEST_OPPONENT'=>entry.fetch(:opponent, 8).to_s,
         'MELEE_TEST_CASE'=>"#{entry[:group]}: #{entry[:name]}", 'MELEE_TEST_CAPTURE'=>'0', 'MELEE_TEST_VIDEO_SECONDS'=>'0', 'MELEE_TEST_APP'=>binary}
  env['MELEE_TEST_ITEM'] = entry[:item].to_s if entry.key?(:item)
  output, status = Open3.capture2e(env, 'ruby', 'native/tools/run_input_test.rb', script, '90')
  File.write(File.join(directory, "#{entry[:id]}-runner.log"), output)
  entry[:run] = output[/artifacts: (.+)/, 1]
  entry[:status] = status.success? ? 'smoke-pass' : 'failed'
  if entry[:run] && File.file?(File.join(entry[:run], 'game.log'))
    log = File.read(File.join(entry[:run], 'game.log'))
    entry[:failure] = log.lines.find { |line| line.match?(/runtime error:|ERROR: AddressSanitizer|\[native-crash\]|Missing native asset|\[input-test\] timed out|assertion.*failed|HSD_ASSERT|Native archive error:|Native game panic/) }&.strip
    entry[:item_spawns] = log.scan(/\[matrix-item\] kind=\d+ spawned=1/).size if entry.key?(:item)
    entry[:status] = 'failed-no-item-spawn' if entry[:status] == 'smoke-pass' && entry.key?(:item) && entry[:item_spawns] == 0
    entry[:match_entered] = log.include?("ready scene 2\n")
    samples = log.scan(/center_nonblack=([0-9.]+)/).flatten.map(&:to_f)
    entry[:render_samples] = samples
    entry[:rendering] = samples.empty? ? 'unmeasured' : samples.max < 0.001 ? 'center-black' : 'center-visible'
    if entry[:status] == 'smoke-pass' && entry[:rendering] != 'center-visible'
      entry[:status] = 'failed-render-check'
    end
    details = log.scan(/\[render-detail\] colors=(\d+) dominant=([0-9.]+) edges=([0-9.]+)/).map do |colors, dominant, edges|
      {'colors'=>colors.to_i, 'dominant'=>dominant.to_f, 'edges'=>edges.to_f}
    end
    entry[:render_details] = details
    combat_details = details.drop(1) # Exclude the READY/GO overlay.
    varied = combat_details.count { |sample| sample['colors'] >= 8 && sample['dominant'] < 0.995 && sample['edges'] > 0.005 }
    if entry[:status] == 'smoke-pass' && (varied < 3 || varied < (combat_details.size * 0.8).ceil)
      entry[:status] = 'failed-flat-or-unmeasured-playfield'
    end
    match_log = log.split("[input-test] ready scene 2\n", 2).last
    entry[:presented_fps] = match_log.scan(/presented_fps=([0-9.]+)/).flatten.map(&:to_f)
  end
  save.call
  puts "#{entry[:status]} #{entry[:name]} #{entry[:failure]}"
  $stdout.flush
  break if ENV.fetch('MELEE_TEST_STOP_ON_FAILURE', '1') == '1' && entry[:status] != 'smoke-pass'
end
puts "Finished: #{directory}/report.json"

exit(cases.all? { |entry| entry[:status] == 'smoke-pass' } ? 0 : 1)
