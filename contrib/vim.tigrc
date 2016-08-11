# Vim-style keybindings for Tig
#
# To use these keybindings copy the file to your HOME directory and include
# it from your ~/.tigrc file:
#
#	$ cp contrib/vim.tigrc ~/.tigrc.vim
#	$ echo "source ~/.tigrc.vim" >> ~/.tigrc

bind generic h scroll-left
bind generic j move-down
bind generic k move-up
bind generic l scroll-right

bind generic g  none
bind generic gg move-first-line
bind generic gj next
bind generic gk previous
bind generic gp parent
bind generic gP back
bind generic gn view-next

bind main    G  none
bind generic G  move-last-line

bind generic <C-f> move-page-down
bind generic <C-b> move-page-up

bind generic v  none
bind generic vm view-main
bind generic vd view-diff
bind generic vl view-log
bind generic vt view-tree
bind generic vb view-blob
bind generic vx view-blame
bind generic vr view-refs
bind generic vs view-status
bind generic vu view-stage
bind generic vy view-stash
bind generic vg view-grep
bind generic vp view-pager
bind generic vh view-help

bind generic o  none
bind generic oo :toggle sort-order
bind generic os :toggle sort-field
bind generic on :toggle line-number
bind generic od :toggle date
bind generic oa :toggle author
bind generic og :toggle line-graphics
bind generic of :toggle file-name
bind generic op :toggle ignore-space
bind generic oi :toggle id
bind generic ot :toggle commit-title-overflow
bind generic oF :toggle file-filter
bind generic or :toggle commit-title-refs

bind generic @  none
bind generic @j :/^@@
bind generic @k :?^@@
bind generic @- :toggle diff-context -1
bind generic @+ :toggle diff-context +1

bind status  u  none
bind stage   u  none
bind generic uu status-update
bind generic ur status-revert
bind generic um status-merge
bind generic ul stage-update-line
bind generic us stage-split-chunk

bind generic c  none
bind generic cc !git commit
bind generic ca !?@git commit --amend --no-edit

bind generic K view-help
bind generic <C-w><C-w> view-next
