# HP85-Detokenizer
# 
# HP-85 BASIC Detokenizer - converts binary BASIC program to ASCII
# 
# Based on code written by Leif Jon Harcke
# 	(http://web.archive.org/web/20130320012825/http://rocknroll.stanford.edu/~lharcke/)
# 
# The original code listed the tokens, but made no effort to combine them into a BASIC program.
# 
# I have added the neccessary code to provide such a listing, but with some provisos, some major, others nits
# The most important perhaps is that adding new keywords is a pain. This needs to be fixed in order to be able
# to add the option ROMs to the basic (no pun intended) set.
# 
# Another visible problem is that there are tons of extra brackets. E.g. D$[J,J+1] is printed as D$[J,(J+2)]
# annoying, but the HP-85 will trim the extra brackets, so the program is still loadable by an HP-85.
# 
# HP-85 BASIC tokens have an 8-bit identifier. I believe that most of them have been allocated, but
# I don't as yet have all of them. In addition there are commands from ROMs.
# 
# This is work in progress hopefully more commands (esp. from ROMs) will be added soon.
# 
# Version 1.0 (7.9.19): can list all programs in the HP-85 standard pac
# 
