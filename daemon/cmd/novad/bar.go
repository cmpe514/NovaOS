package main

// Session helpers for sway:
//
//	novad bar    the status_command of the session's swaybar (i3bar protocol): a large Home button and
//	             the time. Clicking either returns to the shell.
//	novad watch  keeps the bar hidden while the shell (workspace 1) is focused and docked everywhere
//	             else, and returns to the shell when the browser workspace becomes empty — so closing the
//	             last browser window never leaves the user on a blank screen.

import (
	"bufio"
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"os"
	"os/exec"
	"strings"
	"time"
)

type block struct {
	Name       string `json:"name"`
	FullText   string `json:"full_text"`
	Color      string `json:"color,omitempty"`
	Background string `json:"background,omitempty"`
	Border     string `json:"border,omitempty"`
	MinWidth   int    `json:"min_width,omitempty"`
	Align      string `json:"align,omitempty"`
	Separator  bool   `json:"separator"`
	SepWidth   int    `json:"separator_block_width"`
}

type swayNode struct {
	Type     string     `json:"type"`
	Num      int        `json:"num"`
	Focused  bool       `json:"focused"`
	PID      int        `json:"pid"`
	Nodes    []swayNode `json:"nodes"`
	Floating []swayNode `json:"floating_nodes"`
}

func children(n swayNode) []swayNode { return append(append([]swayNode{}, n.Nodes...), n.Floating...) }

func countWindows(n swayNode) int {
	c := 0
	if n.PID > 0 {
		c++
	}
	for _, k := range children(n) {
		c += countWindows(k)
	}
	return c
}

func hasFocus(n swayNode) bool {
	if n.Focused {
		return true
	}
	for _, k := range children(n) {
		if hasFocus(k) {
			return true
		}
	}
	return false
}

// focusedWorkspace returns the focused workspace's number and its window count.
func focusedWorkspace(tree swayNode) (num, windows int, ok bool) {
	var walk func(swayNode) bool
	walk = func(n swayNode) bool {
		if n.Type == "workspace" && hasFocus(n) {
			num, windows, ok = n.Num, countWindows(n), true
			return true
		}
		for _, k := range n.Nodes {
			if walk(k) {
				return true
			}
		}
		return false
	}
	walk(tree)
	return
}

func swaymsg(args ...string) ([]byte, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	return exec.CommandContext(ctx, "swaymsg", args...).Output()
}

func goHome() { _, _ = swaymsg("workspace", "number", "1") }

func bar(args []string) int {
	fs := flag.NewFlagSet("bar", flag.ExitOnError)
	accent := fs.String("accent", "86f4ef", "Home button colour, rrggbb (no leading #: sway runs this line with sh)")
	settingsPath := fs.String("settings", "/var/lib/novaos/data/settings.json", "shell settings (for the 24-hour clock)")
	fs.Parse(args)
	if !strings.HasPrefix(*accent, "#") {
		*accent = "#" + *accent
	}
	clock24 := func() bool {
		var s struct {
			UI struct {
				Clock24 bool `json:"clock24"`
			} `json:"ui"`
		}
		data, err := os.ReadFile(*settingsPath)
		return err == nil && json.Unmarshal(data, &s) == nil && s.UI.Clock24
	}
	out := bufio.NewWriter(os.Stdout)
	emit := func() {
		layout := "3:04 PM"
		if clock24() {
			layout = "15:04"
		}
		blocks := []block{
			{Name: "home", FullText: "  ⌂  Home  ", Color: "#10222c", Background: *accent, Border: *accent, MinWidth: 120, Align: "center", SepWidth: 18},
			{Name: "time", FullText: time.Now().Format(layout), Color: "#ffffff", SepWidth: 6},
		}
		data, _ := json.Marshal(blocks)
		fmt.Fprintf(out, "%s,\n", data)
		out.Flush()
	}
	fmt.Fprintln(out, `{"version":1,"click_events":true}`)
	fmt.Fprintln(out, "[")
	emit()
	// Clicks arrive on stdin as an endless JSON array: "[\n{...}\n,{...}".
	go func() {
		sc := bufio.NewScanner(os.Stdin)
		for sc.Scan() {
			line := strings.TrimLeft(strings.TrimSpace(sc.Text()), "[,")
			var click struct {
				Name string `json:"name"`
			}
			if json.Unmarshal([]byte(line), &click) == nil && (click.Name == "home" || click.Name == "time") {
				goHome()
			}
		}
	}()
	for {
		time.Sleep(time.Second)
		emit()
	}
}

func watch(args []string) int {
	fs := flag.NewFlagSet("watch", flag.ExitOnError)
	barID := fs.String("bar", "novaos", "swaybar id to show and hide")
	interval := fs.Duration("interval", 400*time.Millisecond, "how often to look at sway")
	fs.Parse(args)
	mode := ""
	for {
		if raw, err := swaymsg("-r", "-t", "get_tree"); err == nil {
			var tree swayNode
			if json.Unmarshal(raw, &tree) == nil {
				if num, windows, ok := focusedWorkspace(tree); ok {
					if num != 1 && windows == 0 {
						goHome()
						num = 1
					}
					want := "dock"
					if num == 1 {
						want = "invisible"
					}
					if want != mode {
						if _, err := swaymsg("bar", *barID, "mode", want); err == nil {
							mode = want
						}
					}
				}
			}
		}
		time.Sleep(*interval)
	}
}
