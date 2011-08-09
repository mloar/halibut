;; Halibut mode for emacs.
;;
;; Tested on GNU emacs 23.2.1.

(defun halibut-mode-font-lock-extend-region ()
  (save-excursion
    (let (new-beg new-end)
      (goto-char font-lock-beg)
      (if (re-search-backward "\n[ \t]*\n" nil 1)
          (goto-char (match-end 0)))
      (setq new-beg (point))
      (goto-char font-lock-end)
      (re-search-forward "\n[ \t]*\n" nil 1)
      (setq new-end (point))
      (if (and (= new-beg font-lock-beg) (= new-end font-lock-end))
          nil ;; did nothing
        (setq font-lock-beg new-beg)
        (setq font-lock-end new-end)
        t))))

(defun halibut-mode-match-braced-comment (limit)
  ;; Look for a braced Halibut comment, which starts with \#{ and ends
  ;; with } but has to skip over matching unescaped braces in between.
  (if (not (search-forward "\\#{" limit t))
      nil ;; didn't find the introducer string
    (let ((start (match-beginning 0))
          (depth 1))
      (goto-char (match-end 0))
      ;; Repeatedly find the next unescaped brace and adjust depth.
      (while (and (> depth 0)
                  (looking-at "\\([^\\\\{}]\\|\\\\.\\)*\\([{}]\\)")
                  (< (match-end 2) limit))
        (setq depth (if (string= (match-string 2) "{") (1+ depth) (1- depth)))
        (goto-char (match-end 2)))
      ;; If depth hit zero, we've stopped just after the closing
      ;; brace. If it didn't, we should stop at limit.
      (if (> depth 0) (goto-char limit))
      ;; Now the string between 'start' and point is our match.
      (set-match-data (list start (point)))
      t)))

(defun halibut-mode-match-paragraph-comment (limit)
  ;; Look for a whole-paragraph Halibut comment, which starts with \#
  ;; and then something other than an open brace, and ends at the next
  ;; paragraph break.
  (catch 'found-one
    (while (search-forward "\\#" limit t)
      (let ((start (match-beginning 0)))
        ;; For each \# we find, check to see if it's eligible.
        (when (and
               ;; It must not be followed by {.
               (not (looking-at "\\\\#{"))
               ;; It must be the first thing in its paragraph (either
               ;; because the chunk of whitespace immediately preceding it
               ;; contains more than one \n, or because that chunk of
               ;; whitespace terminates at the beginning of the file).
               (let ((this-line (line-number-at-pos)))
                 (save-excursion
                   (goto-char start)
                   (skip-chars-backward "\n\t ")
                   (or (= (point) (point-min))
                       (< (line-number-at-pos) (1- this-line))))))
          ;; If those conditions are satisfied, we've found an
          ;; eligible \#. Search forward for the next paragraph end.
          (if (re-search-forward "\n[ \t]*\n" nil 1)
              (goto-char (match-beginning 0)))
          ;; Now the string between 'start' and point is our match.
          (set-match-data (list start (point)))
          ;; Terminate the while loop.
          (throw 'found-one t))))
    ;; The loop terminated without finding anything.
    nil))

(defun halibut-mode-match-code-or-emphasis-line (char limit)
  ;; Look for a Halibut code line (starting with "\c " or containing
  ;; only "\c"). Either right here...
  (if (and (= (current-column) 0) (looking-at (concat "\\\\" char "[ \n]")))
      (let ((start (match-beginning 0)))
        (end-of-line)
        (set-match-data (list start (min limit (point))))
        t)
    ;; ... or further down...
    (if (re-search-forward (concat "\n\\\\" char "[ \n]") limit t)
        (let ((start (1+ (match-beginning 0))))
          (goto-char start)
          (end-of-line)
          (set-match-data (list start (min limit (point))))
          t)
      ;; and if neither of those, we didn't find one.
      nil)))

(defun halibut-mode-match-code-line (limit)
  (halibut-mode-match-code-or-emphasis-line "c" limit))
(defun halibut-mode-match-emphasis-line (limit)
  (halibut-mode-match-code-or-emphasis-line "e" limit))

(defconst halibut-font-lock-keywords
  '((halibut-mode-match-braced-comment . font-lock-comment-face)
    (halibut-mode-match-paragraph-comment . font-lock-comment-face)
    (halibut-mode-match-code-line . font-lock-string-face)
    (halibut-mode-match-emphasis-line . font-lock-preprocessor-face)
    ("\\\\\\([-{}_\\\\]\\|u[0-9a-fA-F]*\\|[A-Za-tv-z][0-9A-Za-z]*\\)" .
     font-lock-keyword-face))
  "Syntax highlighting for Halibut mode.")

;;;###autoload
(define-derived-mode halibut-mode fundamental-mode "Halibut"
  "Major mode for editing Halibut documentation markup."
  (setq font-lock-defaults '(halibut-font-lock-keywords t))
  (add-hook 'font-lock-extend-region-functions 'halibut-mode-font-lock-extend-region))

;;;###autoload
(add-to-list 'auto-mode-alist '("\\.but\\'" . halibut-mode))
