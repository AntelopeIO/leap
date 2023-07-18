#!/usr/bin/python3
#

#	Prepare a boost checkout for release
#	1) Copy all the files at the root level to the dest folder ($DEST)
#	2) Copy all the "special" folders to the dest folder ($DEST)
#	3) copy all the files from $SOURCE/libs to $DEST/libs
#	4a) For each subproject, copy everything except "include" into $DEST/libs
#	4b) For each subproject, copy the contents of the "includes" folder into $DEST/boost
#
#	Usage: %0 source dest

from __future__ import print_function

import os, sys
import shutil
import stat
import six
import datetime

IgnoreFiles = shutil.ignore_patterns(
	'[.]*',
	'[.]gitattributes',
	'[.]gitignore',
	'[.]gitmodules',
	'[.]travis[.]yml',
	'appveyor[.]yml',
	'circle[.]yml')

def IgnoreFile(src, name):
	return len(IgnoreFiles(src, [name])) > 0

## from <http://stackoverflow.com/questions/1868714/how-do-i-copy-an-entire-directory-of-files-into-an-existing-directory-using-pyth>
def MergeTree(src, dst, symlinks = False):
	if not os.path.exists(dst):
		os.makedirs(dst)
		shutil.copystat(src, dst)
	lst = os.listdir(src)
	excl = IgnoreFiles(src, lst)
	lst = [x for x in lst if x not in excl]
	for item in lst:
		s = os.path.join(src, item)
		d = os.path.join(dst, item)
		if symlinks and os.path.islink(s):
			if os.path.lexists(d):
				os.remove(d)
			os.symlink(os.readlink(s), d)
			try:
				st = os.lstat(s)
				mode = stat.S_IMODE(st.st_mode)
				os.lchmod(d, mode)
			except:
				pass # lchmod not available
		elif os.path.isdir(s):
			MergeTree(s, d, symlinks)
		else:
			if os.path.exists(d):
				print("## Overwriting file %s with %s" % (d, s))
			shutil.copy2(s, d)


def CopyFile (s, d, f):
	if os.path.isfile(os.path.join(s,f)) and not IgnoreFile(s, f):
		shutil.copy2(os.path.join(s,f), os.path.join(d,f))

def CopyDir (s, d, dd):
	if os.path.isdir(os.path.join(s,dd)) and not IgnoreFile(s, dd):
		shutil.copytree(os.path.join(s,dd), os.path.join(d,dd), symlinks=False, ignore=IgnoreFiles)

def MergeIf(s, d, dd):
# 	if dd == 'detail':
# 		print "MergeIf %s -> %s" % (os.path.join(s, dd), os.path.join(d, dd))
	if os.path.exists(os.path.join(s, dd)):
		MergeTree(os.path.join(s, dd), os.path.join(d, dd), symlinks=False)

def CopyInclude(src, dst):
	for item in os.listdir(src):
		if IgnoreFile(src, item):
			continue
		if item == 'pending':
			continue
		if item == 'detail':
			continue
		s = os.path.join(src, item)
		d = os.path.join(dst, item)
		if os.path.isdir(s):
			MergeTree(s, d, symlinks=False)
		else:
			if os.path.exists(d):
				print("## Overwriting file %s with %s" % (d, s))
			CopyFile(src, dst, item)
	

def CopySubProject(src, dst, headers, p):
	#	First, everything except the "include" directory
	Source = os.path.join(src,p)
	Dest   = os.path.join(dst,p)
	#	print "CopySubProject %p" % p
	os.makedirs(Dest)
	for item in os.listdir(Source):
		if os.path.isfile(os.path.join(Source, item)):
			CopyFile(Source, Dest, item)
		elif item != "include":
			CopyDir(Source, Dest, item)
			
	#shutil.copytree(Source, Dest, symlinks=False, ignore=shutil.ignore_patterns('\.*', "include"))	

	# Now the includes
	Source = os.path.join(src, "%s/include/boost" % p)
	if os.path.exists(Source):
		CopyInclude(Source, headers)
# 		MergeTree(Source, Dest, symlinks=False, ignore=shutil.ignore_patterns('\.*', 'detail', 'pending'))
		MergeIf(Source, headers, 'detail')
		MergeIf(Source, headers, 'pending')


def CopyNestedProject(src, dst, headers, p):
	#	First, everything except the "include" directory
	Source = os.path.join(src,p[1])
	Dest   = os.path.join(dst,p[1])
	os.makedirs(Dest)
	for item in os.listdir(Source):
		if os.path.isfile(os.path.join(Source, item)):
			CopyFile(Source, Dest, item)
		elif item != "include":
			CopyDir(Source, Dest, item)
	# 	shutil.copytree(Source, Dest, symlinks=False, ignore=shutil.ignore_patterns('\.*', "include"))

	Source = os.path.join(src, "%s/include/boost" % (p[1]))
	#  	Dest = os.path.join(headers, p)
	# 	print "Installing headers from %s to %s" % (Source, headers)
	CopyInclude(Source, headers)
	# # 	MergeTree(Source, Dest, symlinks=False, ignore=shutil.ignore_patterns('\.*', 'detail', 'pending'))
	# 	MergeIf(Source, headers, 'detail')
	# 	MergeIf(Source, headers, 'pending')

BoostHeaders = "boost"
BoostLibs = "libs"

BoostSpecialFolders = [ "doc", "more", "status", "tools" ]

SourceRoot = sys.argv[1]
DestRoot   = sys.argv[2]

print("Source = %s" % SourceRoot)
print("Dest   = %s" % DestRoot)

if not os.path.exists(SourceRoot):
	print("## Error: %s does not exist" % SourceRoot)
	exit(1)

if os.path.exists(DestRoot):
    print("The destination directory already exists. All good.\n")
    exit(0)
    #timestamp1 = datetime.datetime.now().strftime("%Y_%m_%d_%H_%M_%S")
    #os.rename(DestRoot,DestRoot + "_bck_" + timestamp1)

if not os.path.exists(DestRoot):
	print("Creating destination directory %s" % DestRoot)
	os.makedirs(DestRoot)

DestHeaders = os.path.join(DestRoot, BoostHeaders)
DestLibs    = os.path.join(DestRoot, BoostLibs)
os.makedirs(DestHeaders)
os.makedirs(DestLibs)

## Step 1
for f in os.listdir(SourceRoot):
	if f != 'CMakeLists.txt':
		CopyFile(SourceRoot, DestRoot, f)

## Step 2
for d in BoostSpecialFolders:
	CopyDir(SourceRoot, DestRoot, d)

## Step 3
SourceLibs = os.path.join(SourceRoot, BoostLibs)
for f in os.listdir(SourceLibs):
	CopyFile(SourceLibs, DestLibs, f)

## Step 4
BoostSubProjects = set()
for f in os.listdir(SourceLibs):
	if os.path.isdir(os.path.join(SourceLibs,f)):
		if os.path.isfile(os.path.join(SourceLibs,f,"meta","libraries.json")):
			BoostSubProjects.add(f)
		elif os.path.isdir(os.path.join(SourceLibs,f,"include")):
			BoostSubProjects.add(f)
		elif f == 'headers':
			BoostSubProjects.add(f)
		elif os.path.isfile(os.path.join(SourceLibs,f,"sublibs")):
			for s in os.listdir(os.path.join(SourceLibs,f)):
				if os.path.isdir(os.path.join(SourceLibs,f,s)):
					if os.path.isfile(os.path.join(SourceLibs,f,s,"meta","libraries.json")):
						BoostSubProjects.add((f,s))
					elif os.path.isdir(os.path.join(SourceLibs,f,s,"include")):
						BoostSubProjects.add((f,s))

for p in BoostSubProjects:
	if isinstance(p, six.string_types):
		CopySubProject(SourceLibs, DestLibs, DestHeaders, p)
	else:
		NestedSource  = os.path.join(SourceRoot,"libs",p[0])
		NestedDest    = os.path.join(DestRoot,"libs",p[0])
		NestedHeaders = os.path.join(DestRoot,"boost")
		if not os.path.exists(NestedDest):
			os.makedirs(NestedDest)
		if not os.path.exists(NestedHeaders):
			os.makedirs(NestedHeaders)
		for f in os.listdir(NestedSource):
			CopyFile(NestedSource, NestedDest, f)
		CopyNestedProject(NestedSource, NestedDest, NestedHeaders, p)
