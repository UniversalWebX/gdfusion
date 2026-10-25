
from __future__ import annotations
import sys
from pathlib import Path
import tkinter as tk
from tkinter import ttk
ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))
from clientcore.registry import FEATURE_REGISTRY

BG="#0c111b"; PANEL="#141d2c"; PANEL2="#1b283a"; TEXT="#edf3ff"; MUTED="#9aaac2"; ACCENT="#6ea8ff"
class Browser:
    def __init__(self, root):
        self.root=root; root.title("GD Client Ultimate • Feature Library"); root.geometry("1120x720"); root.configure(bg=BG)
        top=tk.Frame(root,bg=BG); top.pack(fill="x",padx=18,pady=(16,10))
        tk.Label(top,text="GD CLIENT",font=("Segoe UI",10,"bold"),fg=ACCENT,bg=BG).pack(anchor="w")
        tk.Label(top,text="Feature Library",font=("Segoe UI Semibold",23),fg=TEXT,bg=BG).pack(anchor="w")
        tk.Label(top,text="Browse native tools, experimental hooks, and planned integrations. Status labels are authoritative.",font=("Segoe UI",10),fg=MUTED,bg=BG).pack(anchor="w",pady=(4,0))
        bar=tk.Frame(root,bg=BG); bar.pack(fill="x",padx=18,pady=(0,10))
        self.query=tk.StringVar(); self.category=tk.StringVar(value="All categories"); self.status=tk.StringVar(value="All statuses")
        entry=ttk.Entry(bar,textvariable=self.query,width=38); entry.pack(side="left",ipady=5); entry.bind("<KeyRelease>",lambda e:self.refresh())
        ttk.Combobox(bar,textvariable=self.category,values=("All categories",*FEATURE_REGISTRY.categories()),state="readonly",width=26).pack(side="left",padx=8,ipady=4)
        ttk.Combobox(bar,textvariable=self.status,values=("All statuses","implemented","experimental","not-wired"),state="readonly",width=17).pack(side="left",ipady=4)
        self.category.trace_add("write",lambda *a:self.refresh()); self.status.trace_add("write",lambda *a:self.refresh())
        body=tk.PanedWindow(root,orient="horizontal",bg=BG,sashwidth=5,bd=0); body.pack(fill="both",expand=True,padx=18,pady=(0,18))
        left=tk.Frame(body,bg=PANEL); right=tk.Frame(body,bg=PANEL); body.add(left,minsize=430); body.add(right,minsize=400)
        self.list=tk.Listbox(left,bg=PANEL,fg=TEXT,selectbackground="#244a78",selectforeground="white",font=("Segoe UI",10),bd=0,highlightthickness=0,activestyle="none")
        sb=ttk.Scrollbar(left,orient="vertical",command=self.list.yview); self.list.configure(yscrollcommand=sb.set); self.list.pack(side="left",fill="both",expand=True,padx=(10,0),pady=10); sb.pack(side="right",fill="y",pady=10,padx=(0,6)); self.list.bind("<<ListboxSelect>>",self.show_detail)
        self.detail=tk.Text(right,bg=PANEL,fg=TEXT,font=("Segoe UI",10),wrap="word",bd=0,highlightthickness=0,padx=16,pady=16); self.detail.pack(fill="both",expand=True)
        self.detail.tag_configure("heading",font=("Segoe UI Semibold",17),foreground=TEXT); self.detail.tag_configure("label",font=("Segoe UI",9,"bold"),foreground=ACCENT); self.detail.tag_configure("muted",foreground=MUTED)
        self.ids=[]; self.refresh()
    def refresh(self):
        cat=self.category.get(); status=self.status.get(); cat=None if cat=="All categories" else cat; status=None if status=="All statuses" else status
        items=FEATURE_REGISTRY.search(self.query.get(),category=cat,status=status); self.ids=[x.id for x in items]; self.list.delete(0,"end")
        for item in items: self.list.insert("end",f"[{item.implementation_status.upper()}]  {item.name}  ·  {item.category}")
        self.root.title(f"GD Client Ultimate • Feature Library • {len(items)} results")
        if items: self.list.selection_set(0); self.show_detail()
        else: self.detail.delete("1.0","end"); self.detail.insert("end","No results matched your filters.")
    def show_detail(self,event=None):
        sel=self.list.curselection()
        if not sel or sel[0]>=len(self.ids): return
        item=FEATURE_REGISTRY.require(self.ids[sel[0]]); self.detail.delete("1.0","end")
        self.detail.insert("end",item.name+"\n","heading")
        self.detail.insert("end",f"{item.id}\n\n","muted")
        for label,value in (("CATEGORY",item.category),("IMPLEMENTATION STATUS",item.implementation_status.upper()),("ACTIVATION TYPE",item.activation_type),("RISK LEVEL",item.risk_level.upper()),("READY TO ACTIVATE",str(item.activation_ready)),("SAFE MODE RECOMMENDED",str(item.safe_mode_recommended))):
            self.detail.insert("end",label+"\n","label"); self.detail.insert("end",value+"\n\n")
        self.detail.insert("end","DESCRIPTION\n","label"); self.detail.insert("end",item.description+"\n\n")
        self.detail.insert("end","SETTINGS SCHEMA\n","label"); self.detail.insert("end",str(item.settings_schema or "No per-feature settings declared.")+"\n\n")
        self.detail.insert("end","COMPATIBILITY\n","label"); self.detail.insert("end","\n".join(item.compatibility)+"\n\n")
        self.detail.insert("end",item.notes,"muted")
def main():
    root=tk.Tk(); style=ttk.Style();
    try: style.theme_use("clam")
    except tk.TclError: pass
    style.configure("TCombobox",fieldbackground=PANEL2,background=PANEL2,foreground=TEXT)
    style.configure("TEntry",fieldbackground=PANEL2,foreground=TEXT)
    Browser(root); root.mainloop()
if __name__=="__main__": main()
