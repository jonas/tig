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


_tig () {
  local e
  e=$(dirname ${funcsourcetrace[1]%:*})/git-completion.bash
  if [ -f $e ]; then
    GIT_SOURCING_ZSH_COMPLETION=y . $e
  fi
  e=$(dirname ${funcsourcetrace[1]%:*})/tig-completion.bash
  if [ -f $e ]; then
    . $e
  fi
}
