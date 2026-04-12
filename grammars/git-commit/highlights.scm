(subject) @string
(message) @string

(comment) @comment

(path) @string.special
(branch) @label
(commit) @constant

(change kind: "new file" @keyword)
(change kind: "deleted" @keyword)
(change kind: "modified" @keyword)
(change kind: "renamed" @keyword)

(trailer
  key: (trailer_key) @property
  value: (trailer_value) @string)

(header) @type

[":" "=" "->" (scissors)] @punctuation.delimiter
