#!/bin/bash
show_builds() {
  local dir=$1                      # NOT "PATH": that clobbers the command search path
  echo "Most recent builds: in ${dir}"
  echo

  find "${dir}" -type f -iname "*.32x" -exec stat -f '%m %N' {} + \
    | sort -rn \
    | awk -v now="$(date +%s)" '{
        a=now-$1; $1=""; sub(/^ /,"");
        printf "%2dh %02dm %02ds  %s\n", a/3600, (a%3600)/60, a%60, $0;
      }' | head -3
  echo
}

show_builds ./rom/
show_builds /private/tmp/claude-501/
