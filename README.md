# 808 Verbal

Single-page SAT vocabulary quiz: open `index.html` in a browser, or host this repo as a static site.

## Try it locally

Double-click `index.html`, or from this folder:

```bash
python3 -m http.server 8080
```

Then open http://localhost:8080/

## First push to GitHub

From this folder (after you create an empty repo on GitHub):

```bash
git add .
git commit -m "Initial commit: 808 Verbal quiz"
git branch -M main
git remote add origin https://github.com/YOUR_USERNAME/YOUR_REPO.git
git push -u origin main
```

Use your real repo URL from GitHub (**Code** → HTTPS).

## Publish with GitHub Pages

1. Push this project so `index.html` is at the repository root.
2. In the repo on GitHub: **Settings → Pages**.
3. Under **Build and deployment**, set **Source** to **Deploy from a branch**, choose **`main`** (or `master`) and **`/ (root)`**, then save.
4. After a minute, your site will be at:

   `https://<your-username>.github.io/<repo-name>/`

   If the repo is named `<username>.github.io`, the site URL is `https://<username>.github.io/`.

Updates: commit and push; Pages rebuilds on the next push to the branch you selected.
