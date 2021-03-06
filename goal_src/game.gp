;;-*-Lisp-*-

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Jak 1 Project File
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; This file sets up the OpenGOAL build system for Jak 1.

;;;;;;;;;;;;;;;;;;;;;;;
;; Build system macros
;;;;;;;;;;;;;;;;;;;;;;;

;; use defmacro to define goos macros.
(define defmacro defsmacro)
(define defun desfun)

(defun gc-file->o-file (filename)
  "Get the name of the object file for the given GOAL (*.gc) source file."
  (string-append "out/obj/" (stem filename) ".o")
  )

(defmacro goal-src (src-file &rest deps)
  "Add a GOAL source file with the given dependencies"
  `(defstep :in ,(string-append "goal_src/" src-file)
     ;; use goal compiler
     :tool 'goalc
     ;; will output the obj file
     :out '(,(gc-file->o-file src-file))
     ;; dependencies are the obj files
     :dep '(,@(apply gc-file->o-file deps))
     )
  )

(defun make-src-sequence-elt (current previous prefix)
  "Helper for goal-src-sequence"
  `(defstep :in ,(string-append "goal_src/" prefix current)
     :tool 'goalc
     :out '(,(gc-file->o-file current))
     :dep '(#|"iso/KERNEL.CGO"|#
           ,(gc-file->o-file previous))
     )
  )

(defmacro goal-src-sequence (prefix &key (deps '()) &rest sequence)
  "Add a sequence of GOAL files (each depending on the previous) in the given directory, 
   with all depending on the given deps."
  (let* ((first-thing `(goal-src ,(string-append prefix (first sequence)) ,@deps))
         (result (cons first-thing '()))
         (iter result))

    (let ((prev (first sequence))
          (in-iter (rest sequence)))

      (while (not (null? in-iter))
        ;; (fmt #t "{} dep on {}\n" (first in-iter) prev)
        (let ((next (make-src-sequence-elt (first in-iter) prev prefix)))
          (set-cdr! iter (cons next '()))
          (set! iter (cdr iter))
          )

        (set! prev (car in-iter))
        (set! in-iter (cdr in-iter))
        )
      )

    `(begin ,@result)
    )
  )

(defmacro cgo (output-name desc-file-name &rest objs)
  "Add a CGO with the given output name (in out/iso) and input name (in goal_src/dgos)"
  `(defstep :in ,(string-append "goal_src/dgos/" desc-file-name)
     :tool 'dgo
     :out '(,(string-append "out/iso/" output-name))
     )
  )

(defun tpage-name (id)
  (fmt #f "tpage-{}.go" id)
  )

(defmacro copy-texture (tpage-id)
  `(defstep :in ,(string-append "decompiler_out/raw_obj/" (tpage-name tpage-id))
     :tool 'copy
     :out '(,(string-append "out/obj/" (tpage-name tpage-id)))
     )
  )

(defmacro copy-textures (&rest ids)
  `(begin
    ,@(apply (lambda (x) `(copy-texture ,x)) ids)
    )
  )

(defmacro copy-go (name)
  `(defstep :in ,(string-append "decompiler_out/raw_obj/" name ".go")
     :tool 'copy
     :out '(,(string-append "out/obj/" name ".go"))
     )
  )

(defmacro copy-gos (&rest gos)
  `(begin
    ,@(apply (lambda (x) `(copy-go ,x)) gos)
    )
  )

(defmacro group (name &rest stuff)
  `(defstep :in ""
     :tool 'group
     :out '(,(string-append "GROUP:" name))
     :dep '(,@stuff))
  )

;;;;;;;;;;;;;;;;;;;;;;
;; CGO's
;;;;;;;;;;;;;;;;;;;;;;
(cgo "KERNEL.CGO" "kernel.gd")
(cgo "ENGINE.CGO" "engine.gd")
(cgo "GAME.CGO" "game.gd")

;;;;;;;;;;;;;;;;;
;; GOAL Kernel
;;;;;;;;;;;;;;;;;

;; These are set up with proper dependencies

(goal-src "kernel/gcommon.gc")
(goal-src "kernel/gstring-h.gc")
(goal-src "kernel/gkernel-h.gc"
  "gcommon"
  "gstring-h")
(goal-src "kernel/gkernel.gc"
  "gkernel-h")
(goal-src "kernel/pskernel.gc"
  "gcommon"
  "gkernel-h")
(goal-src "kernel/gstring.gc"
  "gcommon"
  "gstring-h")
(goal-src "kernel/dgo-h.gc")
(goal-src "kernel/gstate.gc"
  "gkernel")


;;;;;;;;;;;;;;;;;;;;;;;;
;; Weird special things
;;;;;;;;;;;;;;;;;;;;;;;;

;; The tpage directory
(defstep :in "assets/tpage-dir.txt"
  :tool 'tpage-dir
  :out '("out/obj/dir-tpages.go")
  )

;; the count file.
(defstep :in "assets/game_count.txt"
  :tool 'game-cnt
  :out '("out/obj/game-cnt.go")
  )


;;;;;;;;;;;;;;;;;;;;;
;; Textures (Common)
;;;;;;;;;;;;;;;;;;;;;
                 
(copy-textures 463 2 880 256 1278 1032 62 1532)


;;;;;;;;;;;;;;;;;;;;;
;; Art (Common)
;;;;;;;;;;;;;;;;;;;;;

(copy-gos
 "fuel-cell-ag"
 "money-ag"
 "buzzer-ag"
 "ecovalve-ag-ART-GAME"
 "crate-ag"
 "speaker-ag"
 "fuelcell-naked-ag"
 "eichar-ag"
 "sidekick-ag"
 "deathcam-ag"
 )

;;;;;;;;;;;;;;;;;;;;;
;; Text
;;;;;;;;;;;;;;;;;;;;;

(defstep :in "assets/game_text.txt"
  :tool 'text
  :out '("out/iso/0COMMON.TXT"
         "out/iso/1COMMON.TXT"
         "out/iso/2COMMON.TXT"
         "out/iso/3COMMON.TXT"
         "out/iso/4COMMON.TXT"
         "out/iso/5COMMON.TXT"
         "out/iso/6COMMON.TXT")
  )


;;;;;;;;;;;;;;;;;;;;;
;; ISO Group
;;;;;;;;;;;;;;;;;;;;;
;; the iso group is a group of files required to boot.

(group "iso"
       "out/iso/0COMMON.TXT"
       "out/iso/KERNEL.CGO"
       "out/iso/GAME.CGO"
       "out/iso/VI1.DGO"
       )


;;;;;;;;;;;;;;;;;;;;;
;; Village 1
;;;;;;;;;;;;;;;;;;;;;

;; the definition for the DGO file.
(cgo "VI1.DGO"
     "vi1.gd"
     )

;; the code
(goal-src-sequence
 "levels/"
 :deps ;; no idea what these depend on, make it depend on the whole engine
 ("out/obj/default-menu.o")

 "village_common/villagep-obs.gc"
 "village_common/oracle.gc"
 "village1/farmer.gc"
 "village1/explorer.gc"
 "village1/assistant.gc"
 "village1/sage.gc"
 "village1/yakow.gc"
 "village1/village-obs-VI1.gc"
 "village1/fishermans-boat.gc"
 "village1/village1-part.gc"
 "village1/village1-part2.gc"
 "village1/sequence-a-village1.gc"
 )

;; the textures
(copy-textures 398 400 399 401 1470)

;; the art
(copy-gos
 "assistant-ag"
 "evilplant-ag"
 "explorer-ag"
 "farmer-ag"
 "fishermans-boat-ag"
 "hutlamp-ag"
 "mayorgears-ag"
 "medres-beach-ag"
 "medres-beach1-ag"
 "medres-beach2-ag"
 "medres-beach3-ag"
 "medres-jungle-ag"
 "medres-jungle1-ag"
 "medres-jungle2-ag"
 "medres-misty-ag"
 "medres-training-ag"
 "medres-village11-ag"
 "medres-village12-ag"
 "medres-village13-ag"
 "oracle-ag-VI1"
 "orb-cache-top-ag-VI1"
 "reflector-middle-ag"
 "revcycle-ag"
 "revcycleprop-ag"
 "ropebridge-32-ag"
 "sage-ag"
 "sagesail-ag"
 "sharkey-ag-VI1"
 "villa-starfish-ag"
 "village-cam-ag-VI1"
 "village1cam-ag"
 "warp-gate-switch-ag-VI1-VI3"
 "warpgate-ag"
 "water-anim-village1-ag"
 "windmill-sail-ag"
 "windspinner-ag"
 "yakow-ag"
 "village1-vis"
 )
 

;;;;;;;;;;;;;;;;;;;;;
;; Game Engine Code
;;;;;;;;;;;;;;;;;;;;;

;; We don't know the actual dependencies, but the build
;; order is a possibly ordering, and the goal-src-sequence
;; will force these to always build in this order.

(goal-src-sequence
 ;; prefix
 "engine/"

 :deps
 ("out/obj/gcommon.o"
  "out/obj/gstate.o"
  "out/obj/gstring.o"
  "out/obj/gkernel.o"
  )

 ;; sources
 "util/types-h.gc"
 "ps2/vu1-macros.gc"
 "math/math.gc"
 "math/vector-h.gc"
 "physics/gravity-h.gc"
 "geometry/bounding-box-h.gc"
 "math/matrix-h.gc"
 "math/quaternion-h.gc"
 "math/euler-h.gc"
 "math/transform-h.gc"
 "geometry/geometry-h.gc"
 "math/trigonometry-h.gc"
 "math/transformq-h.gc"
 "geometry/bounding-box.gc"
 "math/matrix.gc"
 "math/transform.gc"
 "math/quaternion.gc"
 "math/euler.gc"
 "geometry/geometry.gc"
 "math/trigonometry.gc"
 "sound/gsound-h.gc"
 "ps2/timer-h.gc"
 "ps2/timer.gc"
 "ps2/vif-h.gc"
 "dma/dma-h.gc"
 "gfx/hw/video-h.gc"
 "gfx/hw/vu1-user-h.gc"
 "dma/dma.gc"
 "dma/dma-buffer.gc"
 "dma/dma-bucket.gc"
 "dma/dma-disasm.gc"
 "ps2/pad.gc"
 "gfx/hw/gs.gc"
 "gfx/hw/display-h.gc"
 "math/vector.gc"
 "load/file-io.gc"
 "load/loader-h.gc"
 "gfx/texture-h.gc"
 "level/level-h.gc"
 "camera/math-camera-h.gc"
 "camera/math-camera.gc"
 "gfx/font-h.gc"
 "gfx/decomp-h.gc"
 "gfx/hw/display.gc"
 "engine/connect.gc"
 "ui/text-h.gc"
 "game/settings-h.gc"
 "gfx/capture.gc"
 "debug/memory-usage-h.gc"
 "gfx/texture.gc"
 "game/main-h.gc"
 "anim/mspace-h.gc"
 "draw/drawable-h.gc"
 "draw/drawable-group-h.gc"
 "draw/drawable-inline-array-h.gc"
 "draw/draw-node-h.gc"
 "draw/drawable-tree-h.gc"
 "draw/drawable-actor-h.gc"
 "draw/drawable-ambient-h.gc"
 "game/task/game-task-h.gc"
 "game/task/hint-control-h.gc"
 "gfx/generic/generic-h.gc"
 "gfx/lights-h.gc"
 "gfx/ocean/ocean-h.gc"
 "gfx/ocean/ocean-trans-tables.gc"
 "gfx/ocean/ocean-tables.gc"
 "gfx/ocean/ocean-frames.gc"
 "gfx/sky/sky-h.gc"
 "gfx/mood-h.gc"
 "gfx/time-of-day-h.gc"
 "data/art-h.gc"
 "gfx/generic/generic-vu1-h.gc"
 "gfx/merc/merc-h.gc"
 "gfx/merc/generic-merc-h.gc"
 "gfx/tie/generic-tie-h.gc"
 "gfx/generic/generic-work-h.gc"
 "gfx/shadow/shadow-cpu-h.gc"
 "gfx/shadow/shadow-vu1-h.gc"
 "ps2/memcard-h.gc"
 "game/game-info-h.gc"
 "gfx/wind-h.gc"
 "gfx/tie/prototype-h.gc"
 "anim/joint-h.gc"
 "anim/bones-h.gc"
 "engine/engines.gc"
 "data/res-h.gc"
 "data/res.gc"
 "gfx/lights.gc"
 "physics/dynamics-h.gc"
 "target/surface-h.gc"
 "target/pat-h.gc"
 "game/fact-h.gc"
 "anim/aligner-h.gc"
 "game/game-h.gc"
 "game/generic-obs-h.gc"
 "camera/pov-camera-h.gc"
 "util/sync-info-h.gc"
 "util/smush-control-h.gc"
 "physics/trajectory-h.gc"
 "debug/debug-h.gc"
 "target/joint-mod-h.gc"
 "collide/collide-func-h.gc"
 "collide/collide-mesh-h.gc"
 "collide/collide-shape-h.gc"
 "collide/collide-target-h.gc"
 "collide/collide-touch-h.gc"
 "collide/collide-edge-grab-h.gc"
 "draw/process-drawable-h.gc"
 "game/effect-control-h.gc"
 "collide/collide-frag-h.gc"
 "game/projectiles-h.gc"
 "target/target-h.gc"
 "gfx/depth-cue-h.gc"
 "debug/stats-h.gc"
 "gfx/vis/bsp-h.gc"
 "collide/collide-cache-h.gc"
 "collide/collide-h.gc"
 "gfx/shrub/shrubbery-h.gc"
 "gfx/tie/tie-h.gc"
 "gfx/tfrag/tfrag-h.gc"
 "gfx/background-h.gc"
 "gfx/tfrag/subdivide-h.gc"
 "entity/entity-h.gc"
 "gfx/sprite/sprite-h.gc"
 "gfx/shadow/shadow-h.gc"
 "gfx/eye-h.gc"
 "sparticle/sparticle-launcher-h.gc"
 "sparticle/sparticle-h.gc"
 "entity/actor-link-h.gc"
 "camera/camera-h.gc"
 "camera/cam-debug-h.gc"
 "camera/cam-interface-h.gc"
 "camera/cam-update-h.gc"
 "debug/assert-h.gc"
 "ui/hud-h.gc"
 "ui/progress-h.gc"
 "ps2/rpc-h.gc"
 "nav/path-h.gc"
 "nav/navigate-h.gc"
 "load/load-dgo.gc"
 "load/ramdisk.gc"
 "sound/gsound.gc"
 "math/transformq.gc"
 "collide/collide-func.gc"
 "anim/joint.gc"
 "geometry/cylinder.gc"
 "gfx/wind.gc"
 "gfx/vis/bsp.gc"
 "gfx/tfrag/subdivide.gc"
 "gfx/sprite/sprite.gc"
 "gfx/sprite/sprite-distort.gc"
 "debug/debug-sphere.gc"
 "debug/debug.gc"
 "gfx/merc/merc-vu1.gc"
 "gfx/merc/merc-blend-shape.gc"
 "gfx/merc/merc.gc"
 "gfx/ripple.gc"
 "anim/bones.gc"
 "gfx/generic/generic-vu0.gc"
 "gfx/generic/generic.gc"
 "gfx/generic/generic-vu1.gc"
 "gfx/generic/generic-effect.gc"
 "gfx/generic/generic-merc.gc"
 "gfx/generic/generic-tie.gc"
 "gfx/shadow/shadow-cpu.gc"
 "gfx/shadow/shadow-vu1.gc"
 "gfx/depth-cue.gc"
 "gfx/font.gc"
 "load/decomp.gc"
 "gfx/background.gc"
 "draw/draw-node.gc"
 "gfx/shrub/shrubbery.gc"
 "gfx/shrub/shrub-work.gc"
 "gfx/tfrag/tfrag-near.gc"
 "gfx/tfrag/tfrag.gc"
 "gfx/tfrag/tfrag-methods.gc"
 "gfx/tfrag/tfrag-work.gc"
 "gfx/tie/tie.gc"
 "gfx/tie/tie-near.gc"
 "gfx/tie/tie-work.gc"
 "gfx/tie/tie-methods.gc"
 "util/sync-info.gc"
 "physics/trajectory.gc"
 "sparticle/sparticle-launcher.gc"
 "sparticle/sparticle.gc"
 "entity/entity-table.gc"
 "load/loader.gc"
 "game/task/task-control-h.gc"
 "game/game-info.gc"
 "game/game-save.gc"
 "game/settings.gc"
 "ambient/mood-tables.gc"
 "ambient/mood.gc"
 "ambient/weather-part.gc"
 "gfx/time-of-day.gc"
 "gfx/sky/sky-utils.gc"
 "gfx/sky/sky.gc"
 "gfx/sky/sky-tng.gc"
 "level/load-boundary-h.gc"
 "level/load-boundary.gc"
 "level/load-boundary-data.gc"
 "level/level-info.gc"
 "level/level.gc"
 "ui/text.gc"
 "collide/collide-probe.gc"
 "collide/collide-frag.gc"
 "collide/collide-mesh.gc"
 "collide/collide-touch.gc"
 "collide/collide-edge-grab.gc"
 "collide/collide-shape.gc"
 "collide/collide-shape-rider.gc"
 "collide/collide.gc"
 "collide/collide-planes.gc"
 "gfx/merc/merc-death.gc"
 "gfx/water/water-h.gc"
 "camera/camera.gc"
 "camera/cam-interface.gc"
 "camera/cam-master.gc"
 "camera/cam-states.gc"
 "camera/cam-states-dbg.gc"
 "camera/cam-combiner.gc"
 "camera/cam-update.gc"
 "geometry/vol-h.gc"
 "camera/cam-layout.gc"
 "camera/cam-debug.gc"
 "camera/cam-start.gc"
 "draw/process-drawable.gc"
 "game/task/hint-control.gc"
 "ambient/ambient.gc"
 "debug/assert.gc"
 "game/generic-obs.gc"
 "target/target-util.gc"
 "target/target-part.gc"
 "collide/collide-reaction-target.gc"
 "target/logic-target.gc"
 "target/sidekick.gc"
 "game/voicebox.gc"
 "target/target-handler.gc"
 "target/target.gc"
 "target/target2.gc"
 "target/target-death.gc"
 "debug/menu.gc"
 "draw/drawable.gc"
 "draw/drawable-group.gc"
 "draw/drawable-inline-array.gc"
 "draw/drawable-tree.gc"
 "gfx/tie/prototype.gc"
 "collide/main-collide.gc"
 "game/video.gc"
 "game/main.gc"
 "collide/collide-cache.gc"
 "entity/relocate.gc"
 "debug/memory-usage.gc"
 "entity/entity.gc"
 "nav/path.gc"
 "geometry/vol.gc"
 "nav/navigate.gc"
 "anim/aligner.gc"
 "game/effect-control.gc"
 "gfx/water/water.gc"
 "game/collectables-part.gc"
 "game/collectables.gc"
 "game/task/task-control.gc"
 "game/task/process-taskable.gc"
 "camera/pov-camera.gc"
 "game/powerups.gc"
 "game/crates.gc"
 "ui/hud.gc"
 "ui/hud-classes.gc"
 "ui/progress/progress-static.gc"
 "ui/progress/progress-part.gc"
 "ui/progress/progress-draw.gc"
 "ui/progress/progress.gc"
 "ui/credits.gc"
 "game/projectiles.gc"
 "gfx/ocean/ocean.gc"
 "gfx/ocean/ocean-vu0.gc"
 "gfx/ocean/ocean-texture.gc"
 "gfx/ocean/ocean-mid.gc"
 "gfx/ocean/ocean-transition.gc"
 "gfx/ocean/ocean-near.gc"
 "gfx/shadow/shadow.gc"
 "gfx/eye.gc"
 "util/glist-h.gc"
 "util/glist.gc"
 "debug/anim-tester.gc"
 "debug/viewer.gc"
 "debug/part-tester.gc"
 "debug/default-menu.gc"
 )

(goal-src-sequence
 "levels/common/"
 :deps ("out/obj/default-menu.o")
 "texture-upload.gc"
 "rigid-body-h.gc"
 "water-anim.gc"
 "dark-eco-pool.gc"
 "rigid-body.gc"
 "nav-enemy-h.gc"
 "nav-enemy.gc"
 "baseplat.gc"
 "basebutton.gc"
 "tippy.gc"
 "joint-exploder.gc"
 "babak.gc"
 "sharkey.gc"
 "orb-cache.gc"
 "plat.gc"
 "plat-button.gc"
 "plat-eco.gc"
 "ropebridge.gc"
 "ticky.gc"
 )


