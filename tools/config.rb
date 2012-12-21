#!/usr/bin/env ruby

# Processes the configuration file passed as the first argument and
# generates sea_defines.h and sea_defines.inc, for inclusion by config.h 
# and make.inc repectively

if $*.size == 0
	$stderr.puts "usage: #{$0} <config file>"
	exit 1
end

hheader = <<DOC
/* This file was automatically created by config.rb
 * DO NOT edit this file directly! Instead, adjust the configuration
 * and re-process the config file!
 */

DOC
incheader = <<DOC
# This file was automatically created by config.rb
# DO NOT edit this file directly! Instead, adjust the configuration
# and re-process the config file!

DOC

input = $*[0]

file = File.open(input, "r")
h_file = File.open("sea_defines.h", "w")
inc_file = File.open("sea_defines.inc", "w")
File.truncate("sea_defines.h", 0)
File.truncate("sea_defines.inc", 0)
h_file.puts(hheader)
inc_file.puts(incheader)
file.each do |line|
	# get rid of comments and empty lines
	hash = line.index("#")
	if hash != nil
		line.slice!(hash..-1)
	end
	line.chomp!
	if line == "" || line.nil?
		next
	end
	
	symbol = line.split("=")
	key = symbol[0].strip
	value = symbol[1].strip
	h_val = case value
		when "y", "Y" then 1
		when "n", "N" then 0
		else '"' + value + '"'
	end
	
	h_file.puts("#define " + key + " " + h_val.to_s)
	inc_file.puts(key + "=" + h_val.to_s)
end
file.close
h_file.close
inc_file.close
