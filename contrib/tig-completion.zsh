#compdef tig
#
# zsh completion wrapper for tig
# ==============================
#
# You need to install this script to zsh fpath with tig-completion.bash.
#
# The recommended way to install this script is to copy this and tig-completion.bash
# to '~/.zsh/_tig' and '~/.zsh/tig-completion.bash' and
# then add following to your ~/.zshrc file:
#
#  fpath=(~/.zsh $fpath)
#
# You also need Git's Zsh completion installed:
#
# https://github.com/felipec/git-completion/blob/master/git-completion.zsh


_tig () {
  local e

  compdef _git tig

  e=$(dirname ${funcsourcetrace[1]%:*})/tig-completion.bash
  if [ -f $e ]; then
    # Temporarily override __git_complete so the bash script doesn't complain
    local old="$functions[__git_complete]"
    functions[__git_complete]=:
    . $e
    functions[__git_complete]="$old"
  fi
}
