#!/bin/sh
############################################################
#
# sourced by 'put'
#
# $Id: pump.sh,v 1.5 2005/01/28 00:23:51 sourcer Exp $
############################################################

[ -z $COMMON_DEFINED ] && source common

#the same as uniq but working with any - not adjacent only lines.
#we are not supposing to have lines far (ls is already sorted) and we have
#swap anyway ;)
uniq2(){
	awk '{
		found = 0
		for ( i=0; i<nlines; i++ )
			if (lines[i] == $0) {
				found = 1
				break
			}
		if (!found) {
			lines[nlines++] = $0
			print
		}
	}' $@
}

expandIfNeeded(){

	#mark this directory
    echo "balanced" > "$KEYPATH/AIS"
	
    #find alphabet for files which are not in dirs:
	local indexOfLetter=`expr $depth - 1`
	
	#TODO please optimize me!
    local alphabet=`find "$KEYPATH" -maxdepth 1 -type f -print | 
		sed -e 's/.*\///' |
		sed -e "/^.\{0,$depth\}$/d" |
		sed -e "s/^.\{$indexOfLetter\}\(.\)\{1\}.*/\1/" | 
		tr 'A-Z' 'a-z' | 
		uniq2` 
	
[ $DEBUG -gt 0 ] && echo "Moving files begining from: $alphabet from $KEYPATH"

	for char in $alphabet ;do
    	#(notice that we do not need to trim RCS, CVS, .svn dirs since
		#we are dealing with files only, non-recursively)

		local CHAR=`echo $char | tr 'a-z' 'A-Z'`
		local newDir="$KEYPATH/$CHAR"
		
		[ ! -d "$newDir" ] && {
			mkdir "$newDir"
		}
		find "$KEYPATH" -maxdepth 1 -type f \! -name AIS | grep -e ".*/[^/]\{$indexOfLetter\}[$char$CHAR][^/]*$" | xargs -i mv "{}" "$newDir/"  
		#somehow find+grep+xargs works but 'find -regex -exec' - was not
		
				
	done
	
}
