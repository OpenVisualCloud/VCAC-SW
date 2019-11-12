#!/bin/bash
# to be sourced from other scripts
# file system utilities, such as find wrapper

# same as find, but ensures that there is at least one output line (file or dir)
# output: first found entry
# follows symlinks by default
find_any(){
(
	set +o pipefail # ignore writing to closed pipe error, common for 'find | sth-short-lived'
	local A
	find -L "$@" | {
		read -r A || {
			echo -e "No matches found\n(was looking for: find -L $*)" >&2
			return 3
		}
		echo "${A}"
	}
)
}

# same as find, but ensure that there is exactly one output line (file or dir)
# follows symlinks by default
find_single(){
(
	set +o pipefail # ignore writing to closed pipe error, common for 'find | sth-short-lived'
	local A
	find -L "$@" | {
		read -r A || {
			echo -e "No matches found\n(was looking for: find -L $*)" >&2
			return 3
		}
		read -r && {
			echo -e "Multiple matches found, but exactly 1 is required\n(was looking for: find -L $*)" >&2
			return 2
		}
		echo "${A}"
	}
)
}
