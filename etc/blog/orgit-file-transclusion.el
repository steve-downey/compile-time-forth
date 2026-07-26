;; etc/blog/orgit-file-transclusion.el                       -*-Emacs-Lisp-*-
;; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
;; Provenance: docs/epistolary-pinning-plan.md section 3, adapted per its
;; instruction to match this repository's existing worktree adapter.

;;; orgit-file-transclusion.el --- transclude UUID-anchored regions at a git rev

;; Link form: [[orgit-file:REPO::REV::PATH::UUID]]
;; Used with: :lines 2- :src LANG :end "UUID end"   (unchanged conventions)
;;
;; Blog posts are diary entries: the code in one must stay the code that entry
;; was written against.  A worktree-resolved transclusion shows the file as of
;; now, so a later refactor silently rewrites code inside an older post while
;; its prose stays frozen.  This resolver pins each post to a revision --- in
;; practice a `blog/part-NN' tag, see docs/blog/pins.md.  Living documents keep
;; the worktree link forms and go on rolling forward, which is what they are
;; for.
;;
;; Adaptation from the plan's sketch: the plan hands org-transclusion a
;; `:src-content' payload it slices itself.  This repository's worktree adapter
;; (`org-transclusion-add-orgit' in .emacs.d/init.el) instead rewrites the link
;; to a `file:' link with a search option and returns nil, letting the stock
;; file resolver apply :lines/:src/:end.  Doing the same here --- materializing
;; the blob under a cache directory and pointing a `file:' link at it --- is
;; what the plan asks for by "same hook, same attribute handling": every post
;; attribute keeps meaning exactly what it meant against the worktree, because
;; it is handled by exactly the same code.  It also needs neither the
;; orgit-file package nor magit, so batch export stays light.

(require 'org-transclusion)
(require 'subr-x)

(defvar orgit-file-tc-cache-dir
  (expand-file-name "orgit-file-tc" temporary-file-directory)
  "Directory under which blobs read out of git are materialized.
Keyed by resolved commit SHA, so a moved tag can never serve stale text.")

(defvar orgit-file-forge-url nil
  "Base URL of the forge blob view, ending in a slash, or nil.
When non-nil, exported orgit-file links become permalinks of the form
BASE + REV + \"/\" + PATH.  Set it where the repository is named --- the
module itself stays repository-agnostic.")

(defun orgit-file-tc--parse (raw)
  "Split RAW into (REPO REV PATH UUID); error on any other shape."
  (let ((parts (split-string raw "::")))
    (unless (= 4 (length parts))
      (error "orgit-file link needs REPO::REV::PATH::UUID, got: %s" raw))
    parts))

(defun orgit-file-tc--repo (repo)
  "Resolve REPO to a git working directory.
Use the literal REPO field when it names an existing checkout.  When it does
not --- a clone made elsewhere, or a worktree since renamed --- fall back to
the repository containing the document being exported, which holds the same
objects and therefore the same pinned blobs."
  (let ((named (file-name-as-directory (expand-file-name repo))))
    (if (file-exists-p (expand-file-name ".git" named))
        named
      ;; `locate-dominating-file' can hand back an abbreviated name, and git -C
      ;; does not expand a leading tilde.
      (let ((root (locate-dominating-file default-directory ".git")))
        (if root
            (expand-file-name root)
          (error "orgit-file: %s does not exist and no repository encloses %s"
                 repo default-directory))))))

(defun orgit-file-tc--git (repo &rest args)
  "Run git ARGS in REPO, returning stdout; error with stderr on failure."
  (with-temp-buffer
    (unless (zerop (apply #'call-process "git" nil t nil "-C" repo args))
      (error "orgit-file: git %s failed in %s: %s"
             (string-join args " ") repo (string-trim (buffer-string))))
    (buffer-string)))

(defun orgit-file-tc--blob-file (repo rev path)
  "Materialize PATH at REV of REPO under `orgit-file-tc-cache-dir'.
Return the file name.  PATH is kept intact so the extension --- which is what
tells the source resolver how to fence the region --- survives."
  (let* ((sha (string-trim
               (orgit-file-tc--git repo "rev-parse" (concat rev "^{commit}"))))
         (out (expand-file-name (concat sha "/" path) orgit-file-tc-cache-dir)))
    (unless (file-exists-p out)
      (make-directory (file-name-directory out) t)
      (let ((content (orgit-file-tc--git repo "show" (concat sha ":" path))))
        (with-temp-file out (insert content))))
    out))

(defun orgit-file-transclusion-add (link _plist)
  "Resolve an orgit-file LINK to a file link against a materialized blob.
Mirrors `org-transclusion-add-orgit': rewrite LINK in place, return nil, and
let the stock resolver do the rest.  A pin that cannot be read is an error,
not an empty block --- a silent miss is the failure this whole mechanism
exists to make impossible."
  (when (string= "orgit-file" (org-element-property :type link))
    (pcase-let* ((`(,repo ,rev ,path ,uuid)
                  (orgit-file-tc--parse (org-element-property :path link)))
                 (file (orgit-file-tc--blob-file
                        (orgit-file-tc--repo repo) rev path)))
      (org-element-put-property link :type "file")
      (org-element-put-property link :path file)
      (org-element-put-property link :raw-link (concat "file:" file "::" uuid))
      (org-element-put-property link :search-option uuid)))
  nil)

(defun orgit-file-tc-export (path desc backend)
  "Export an orgit-file link PATH with description DESC for BACKEND."
  (pcase-let* ((`(,_repo ,rev ,file ,_uuid) (orgit-file-tc--parse path))
               (url (if orgit-file-forge-url
                        (concat orgit-file-forge-url rev "/" file)
                      file)))
    (pcase backend
      ((or 'md 'gfm) (format "[`%s`](%s)" (or desc file) url))
      ('html (format "<a href=\"%s\"><code>%s</code></a>" url (or desc file)))
      (_ url))))

(org-link-set-parameters "orgit-file" :export #'orgit-file-tc-export)

(add-hook 'org-transclusion-add-functions #'orgit-file-transclusion-add)

(provide 'orgit-file-transclusion)
