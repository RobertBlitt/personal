# Smart Electronics Lab

Product design doc for a guided breadboard learning system that grows into a
three-kit product ladder: a quiet electronics lab, an internet-radio
capstone, an SDR/waterfall add-on, and a full SDR workstation.

- `Smart_Electronics_Lab_v1.3_Product_Design.pdf` — formatted deliverable
- `Smart_Electronics_Lab_v1.3_Product_Design.docx` — editable version
- `Smart_Electronics_Lab_v1.3_Product_Design.html` — source; edit this and
  re-render the PDF/docx from it if the content changes

v1.3 restructures the v1.2 three-stage roadmap into a three-kit product
ladder (Kit 1 lab + radio capstone, Kit 2 SDR/waterfall add-on, Kit 3 SDR
workstation), keeps Kit 1 free of any dependency on Elecrow audio by moving
the internet-radio capstone to a dedicated second ESP32 audio module, and
splits Kit 1 into customer-facing Standard/Deluxe tiers. It also adds a
deferred "future hardware convergence" direction for a custom dial/display
module, gated on the MVP being proven first. See Section 0, "What changed in
v1.3," and Section 1, "Decision log," in the document itself. Earlier
versions are available in git history.
