#!/usr/bin/env ruby
require 'json'
require 'fileutils'

output = File.expand_path(ARGV.shift || abort('Usage: summarize_matrix.rb OUTPUT_DIR REPORT.json ...'))
abort('Supply at least one matrix report') if ARGV.empty?
reports = ARGV.map { |path| [File.expand_path(path), JSON.parse(File.read(path))] }
cases = reports.flat_map do |path, report|
  report.fetch('cases').select { |entry| entry['status'] }.map { |entry| entry.merge('report' => path, 'binary' => report.fetch('binary')) }
end
attempts = cases
# A retry supersedes the same planned case; preserve all attempts in JSON.
cases = cases.group_by { |entry| [entry['group'], entry['id']] }.values.map(&:last)
summary = {
  'coverage' => '900-frame match samples (about 15 seconds including countdown), two CPU players, default costumes. Items are spawned repeatedly. Numeric central-playfield GPU checks; no saved images.',
  'limits' => 'Not exhaustive visual or gameplay acceptance. Does not cover all moves, combinations, costumes, Kirby copy abilities, Pokemon variants, adventure/target stages or saving.',
  'counts' => cases.group_by { |entry| entry.fetch('group') }.transform_values do |entries|
    { 'total' => entries.size, 'passed' => entries.count { |entry| entry['status'] == 'smoke-pass' } }
  end,
  'unique_coverage' => {
    'stages' => cases.select { |c| c['status'] == 'smoke-pass' }.map { |c| c['stage'] }.uniq.sort,
    'characters' => cases.select { |c| c['status'] == 'smoke-pass' }.flat_map { |c| [c['character'], c.fetch('opponent', 8)] }.uniq.sort,
    'items' => cases.select { |c| c['status'] == 'smoke-pass' && c.key?('item') }.map { |c| c['item'] }.uniq.sort
  },
  'executable_hashes' => cases.map { |entry| entry['sha256'] }.uniq,
  'attempts' => attempts,
  'cases' => cases
}
expected = {'stages' => (2..32).to_a - [21, 26], 'characters' => (0..25).to_a, 'items' => (0..34).to_a}
summary['missing_coverage'] = expected.to_h do |group, ids|
  [group, ids - summary['unique_coverage'].fetch(group)]
end
summary['complete'] = summary['missing_coverage'].values.all?(&:empty?) && cases.all? { |c| c['status'] == 'smoke-pass' }
FileUtils.mkdir_p(output)

File.write(File.join(output, 'report.json'), JSON.pretty_generate(summary) + "\n")
lines = ['# Native VS test results', '', summary['coverage'], '', summary['limits'], '']
summary['counts'].each { |group, counts| lines << "- #{group}: #{counts['passed']}/#{counts['total']} smoke passes." }
summary['unique_coverage'].each do |group, ids|
  lines << "- Unique #{group} exercised in passing matches: #{ids.size} (IDs #{ids.join(', ')})."
end
lines += ['', 'Executable SHA-256: ' + summary['executable_hashes'].map { |hash| "`#{hash}`" }.join(', '), '',
          '| Group | Case | Result | Match FPS range | Log |', '| --- | --- | --- | --- | --- |']
cases.each do |entry|
  fps = entry['presented_fps'] || []
  range = fps.empty? ? 'unmeasured' : format('%.1f–%.1f', fps.min, fps.max)
  log = entry['run'] ? "[game.log](<#{entry['run']}/game.log>)" : 'missing'
  lines << "| #{entry['group']} | #{entry['name']} | #{entry['status'] || 'not run'} | #{range} | #{log} |"
end
File.write(File.join(output, 'REPORT.md'), lines.join("\n") + "\n")
puts File.join(output, 'REPORT.md')
exit(summary['complete'] ? 0 : 1)
