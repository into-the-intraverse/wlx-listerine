" Vim script sample
set nocompatible
syntax on

let g:max_count = 100
let s:name = "sample"

function! Greet(who) abort
    echomsg "Hello, " . a:who . "!"
    return 1
endfunction

if has('autocmd')
    autocmd BufRead *.txt setlocal wrap
endif

call Greet(s:name)
