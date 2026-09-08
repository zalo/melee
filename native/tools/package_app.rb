#!/usr/bin/env ruby
# Assemble only the executable, its libraries and notices, never the build tree.
require 'fileutils'
require 'open3'
require 'json'
require 'digest'
require 'tmpdir'

root = File.expand_path('../..', __dir__)
build = File.expand_path(ARGV.fetch(0, 'build/native-app'), root)
destination = File.expand_path(ARGV.fetch(1, 'dist'), root)
source = File.join(build, 'melee_mac.app')
app = File.join(destination, 'Melee Native.app')
identity = ENV.fetch('MELEE_SIGN_IDENTITY', '-')
notary_profile = ENV['MELEE_NOTARY_PROFILE']
raise 'Notarization requires MELEE_SIGN_IDENTITY (Developer ID Application)' if notary_profile && identity == '-'
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
%w[MeleeNative.icns SSBM.png].each do |name|
  FileUtils.cp(File.join(root, 'native/resources', name), File.join(resources, name))
end
readme = File.read(File.join(root, 'native/PACKAGE_README.txt'))
if identity != '-'
  readme = readme.sub('The app is locally ad-hoc signed, not Developer ID signed or Apple-notarized.',
    notary_profile ? 'The app is Developer ID signed and Apple-notarized.' : 'The app is Developer ID signed but not Apple-notarized.')
end
File.write(File.join(resources, 'Read Me.txt'), readme)
notices = File.join(resources, 'Third Party Notices')
FileUtils.mkdir_p(notices)
FileUtils.cp(File.join(root, 'native/resources/README.md'), File.join(notices, 'App Artwork.md'))
license_files = {
  'Aurora.txt' => 'build/native-deps/aurora/LICENSE',
  'Dawn.txt' => 'native/licenses/Dawn.txt',
  'nod.txt' => 'native/licenses/nod.txt',
  'FreeType.txt' => 'native/licenses/FreeType.txt',
  'fmt.txt' => '/opt/homebrew/opt/fmt/LICENSE',
  'libpng.txt' => '/opt/homebrew/opt/libpng/LICENSE',
  'zstd.txt' => '/opt/homebrew/opt/zstd/LICENSE'
}
license_files.merge!(File.readlines(File.join(build, 'package-licenses.txt'), chomp: true).to_h { |line| line.split("\t", 2) })
license_files.each { |name, path| FileUtils.cp(File.expand_path(path, root), File.join(notices, name)) }

# Ensure relocation succeeded, including transitive libraries.
([binary] + copied.keys.map { |name| File.join(frameworks, name) }).each do |file|
  dependencies = run('otool', '-L', file).lines.drop(1).map(&:strip)
  unless dependencies.all? { |line| line.start_with?('/System/Library/', '/usr/lib/', '@executable_path/../Frameworks/') }
    raise "Nonportable dependencies: #{file}"
  end
  signing = identity == '-' ? [] : ['--options', 'runtime', '--timestamp']
  run('codesign', '--force', '--sign', identity, *signing, file)
end
signing = identity == '-' ? [] : ['--options', 'runtime', '--timestamp']
run('codesign', '--force', '--sign', identity, *signing, app)
run('codesign', '--verify', '--deep', '--strict', app)
if notary_profile
  submission = File.join(destination, 'notarization.zip')
  run('ditto', '-c', '-k', '--keepParent', app, submission)
  run('xcrun', 'notarytool', 'submit', submission, '--keychain-profile', notary_profile, '--wait')
  run('xcrun', 'stapler', 'staple', app)
  run('xcrun', 'stapler', 'validate', app)
  FileUtils.rm(submission)
end
files = Dir.glob(File.join(app, '**/*')).select { |path| File.file?(path) }
manifest = files.to_h { |path| [path.delete_prefix(destination + '/'), Digest::SHA256.file(path).hexdigest] }
File.write(File.join(destination, 'manifest.json'), JSON.pretty_generate(manifest) + "\n")
archive = File.join(destination, 'Melee-Native-macOS-arm64.zip')
raise "Archive already exists: #{archive}" if File.exist?(archive)
run('ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', app, archive)
dmg = File.join(destination, 'Melee-Native-macOS-arm64.dmg')
raise "Disk image already exists: #{dmg}" if File.exist?(dmg)
Dir.mktmpdir('melee-dmg-') do |staging|
  run('ditto', app, File.join(staging, 'Melee Native.app'))
  File.symlink('/Applications', File.join(staging, 'Applications'))
  File.write(File.join(staging, 'Install.txt'), "Drag Melee Native to Applications, then open it from Applications.\nChoose your own Melee US 1.02 image once, then click Play.\n")
  run('hdiutil', 'create', '-volname', 'Melee Native', '-srcfolder', staging, '-format', 'UDZO', dmg)
end
if notary_profile
  run('codesign', '--sign', identity, '--timestamp', dmg)
  run('xcrun', 'notarytool', 'submit', dmg, '--keychain-profile', notary_profile, '--wait')
  run('xcrun', 'stapler', 'staple', dmg)
  run('xcrun', 'stapler', 'validate', dmg)
end
puts "Packaged #{app}\n#{archive}\n#{dmg}\n#{files.length} files; #{copied.length} bundled libraries; no disc or extracted asset files."
