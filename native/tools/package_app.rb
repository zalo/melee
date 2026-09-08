#!/usr/bin/env ruby
# Assemble only the executable, its libraries and notices, never the build tree.
require 'fileutils'
require 'open3'
require 'json'
require 'digest'

root = File.expand_path('../..', __dir__)
build = File.expand_path(ARGV.fetch(0, 'build/native-app'), root)
destination = File.expand_path(ARGV.fetch(1, 'dist'), root)
source = File.join(build, 'melee_mac.app')
app = File.join(destination, 'Melee Native.app')
raise "Build missing: #{source}" unless File.file?(File.join(source, 'Contents/MacOS/melee_mac'))
raise "Output already exists: #{app}" if File.exist?(app)

def run(*args)
  output, status = Open3.capture2e(*args)
  raise "#{args.first} failed: #{output}" unless status.success?
  output
end

FileUtils.mkdir_p(File.join(app, 'Contents/MacOS'))
FileUtils.cp(File.join(source, 'Contents/Info.plist'), File.join(app, 'Contents/Info.plist'))
binary = File.join(app, 'Contents/MacOS/melee_mac')
FileUtils.cp(File.join(source, 'Contents/MacOS/melee_mac'), binary)
FileUtils.chmod(0755, binary)
run('strip', '-S', binary)
frameworks = File.join(app, 'Contents/Frameworks')
FileUtils.mkdir_p(frameworks)
pending = [binary]
copied = {}
until pending.empty?
  file = pending.shift
  dependencies = run('otool', '-L', file).lines.drop(1).map { |line| line.strip.split(' (compatibility').first }
  dependencies.each do |dependency|
    next if dependency.start_with?('/System/Library/', '/usr/lib/')
    raise "Development runtime in package: #{dependency}" if dependency.include?('clang_rt')
    raise "Unresolved library: #{dependency}" unless dependency.start_with?('/') && File.file?(dependency)
    name = File.basename(dependency)
    if copied[name] && File.realpath(copied[name]) != File.realpath(dependency)
      raise "Library name collision: #{name}"
    end
    unless copied[name]
      copied[name] = dependency
      target = File.join(frameworks, name)
      FileUtils.cp(dependency, target)
      FileUtils.chmod(0755, target)
      pending << target
    end
    replacement = "@executable_path/../Frameworks/#{name}"
    run('install_name_tool', '-change', dependency, replacement, file)
  end
end
copied.each_key do |name|
  run('install_name_tool', '-id', "@executable_path/../Frameworks/#{name}", File.join(frameworks, name))
end

resources = File.join(app, 'Contents/Resources')
FileUtils.mkdir_p(resources)
FileUtils.cp(File.join(root, 'native/PACKAGE_README.txt'), File.join(resources, 'Read Me.txt'))
notices = File.join(resources, 'Third Party Notices')
FileUtils.mkdir_p(notices)
license_files = {
  'Aurora.txt' => 'build/native-deps/aurora/LICENSE',
  'SDL.txt' => 'build/native/_deps/sdl-src/LICENSE.txt',
  'ImGui.txt' => 'build/native/_deps/imgui-src/LICENSE.txt',
  'Abseil.txt' => 'build/native/_deps/abseil-cpp-src/LICENSE',
  'Tracy.txt' => 'build/native/_deps/tracy-src/LICENSE',
  'xxHash.txt' => 'build/native/_deps/xxhash-src/LICENSE',
  'Dawn.txt' => 'native/licenses/Dawn.txt',
  'nod.txt' => 'native/licenses/nod.txt',
  'FreeType.txt' => 'native/licenses/FreeType.txt',
  'fmt.txt' => '/opt/homebrew/opt/fmt/LICENSE',
  'libpng.txt' => '/opt/homebrew/opt/libpng/LICENSE',
  'zstd.txt' => '/opt/homebrew/opt/zstd/LICENSE'
}
license_files.each { |name, path| FileUtils.cp(File.expand_path(path, root), File.join(notices, name)) }

# Ensure relocation succeeded, including transitive libraries.
([binary] + copied.keys.map { |name| File.join(frameworks, name) }).each do |file|
  dependencies = run('otool', '-L', file).lines.drop(1).map(&:strip)
  unless dependencies.all? { |line| line.start_with?('/System/Library/', '/usr/lib/', '@executable_path/../Frameworks/') }
    raise "Nonportable dependencies: #{file}"
  end
  run('codesign', '--force', '--sign', '-', file)
end
run('codesign', '--force', '--sign', '-', app)
run('codesign', '--verify', '--deep', '--strict', app)
files = Dir.glob(File.join(app, '**/*')).select { |path| File.file?(path) }
manifest = files.to_h { |path| [path.delete_prefix(destination + '/'), Digest::SHA256.file(path).hexdigest] }
File.write(File.join(destination, 'manifest.json'), JSON.pretty_generate(manifest) + "\n")
archive = File.join(destination, 'Melee-Native-macOS-arm64.zip')
raise "Archive already exists: #{archive}" if File.exist?(archive)
run('ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', app, archive)
puts "Packaged #{app}\n#{archive}\n#{files.length} files; #{copied.length} bundled libraries; no disc or extracted asset files."
