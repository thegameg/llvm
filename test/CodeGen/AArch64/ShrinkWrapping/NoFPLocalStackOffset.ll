target datalayout = "e-m:o-i64:64-i128:128-n32:64-S128"
target triple = "arm64-apple-ios10.3.0"

@grlibs = external local_unnamed_addr global [750 x i32], align 4
@grnbp = external global [750 x i32], align 4
@list = external local_unnamed_addr global [13595 x i32], align 4
@links = external local_unnamed_addr global [13595 x i32], align 4
@grcolor = external local_unnamed_addr global [750 x i32], align 4
@board = external local_unnamed_addr global [842 x i32], align 4
@lnbn = external local_unnamed_addr global [841 x i32], align 4
@grlbp = external global [750 x i32], align 4
@kosquare = external local_unnamed_addr global i32, align 4
@lnbf = external local_unnamed_addr global [841 x [2 x i32]], align 4
@nblbp = external local_unnamed_addr global [841 x i32], align 4
@fdir = external local_unnamed_addr global [841 x i32], align 4
@ldir = external local_unnamed_addr global [52 x i32], align 4
@nbr = external local_unnamed_addr global [0 x i32], align 4
@grsize = external local_unnamed_addr global [750 x i32], align 4
@mvv = external local_unnamed_addr global [80 x i32], align 4
@mvl = external local_unnamed_addr global [80 x i32], align 4
@edge = external local_unnamed_addr global [842 x i32], align 4

; Function Attrs: norecurse nounwind ssp
; CHECK-LABEL: BB#16:
; CHECK-NEXT: stp x27, x26, [sp, #24]
define i32 @def_atk_nbr(i32, i32, i32, i32, i32, i32, i32) local_unnamed_addr #0 {
  %8 = sext i32 %1 to i64
  %9 = getelementptr inbounds [750 x i32], [750 x i32]* @grlibs, i64 0, i64 %8
  %10 = load i32, i32* %9, align 4, !tbaa !2
  %11 = mul i32 %10, -10
  %12 = add i32 %11, 150
  switch i32 %10, label %55 [
    i32 2, label %13
    i32 1, label %18
  ]

; <label>:13:                                     ; preds = %7
  %14 = getelementptr inbounds [750 x i32], [750 x i32]* @grsize, i64 0, i64 %8
  %15 = load i32, i32* %14, align 4, !tbaa !2
  %16 = mul nsw i32 %15, 50
  %17 = add nsw i32 %16, %12
  br label %55

; <label>:18:                                     ; preds = %7
  %19 = getelementptr inbounds [750 x i32], [750 x i32]* @grlbp, i64 0, i64 %8
  %20 = load i32, i32* %19, align 4, !tbaa !2
  %21 = sext i32 %20 to i64
  %22 = getelementptr inbounds [13595 x i32], [13595 x i32]* @list, i64 0, i64 %21
  %23 = load i32, i32* %22, align 4, !tbaa !2
  %24 = sext i32 %23 to i64
  %25 = getelementptr inbounds [841 x i32], [841 x i32]* @lnbn, i64 0, i64 %24
  %26 = load i32, i32* %25, align 4, !tbaa !2
  %27 = mul nsw i32 %26, 100
  %28 = getelementptr inbounds [750 x i32], [750 x i32]* @grsize, i64 0, i64 %8
  %29 = load i32, i32* %28, align 4, !tbaa !2
  %30 = icmp sgt i32 %29, 1
  %31 = add nsw i32 %29, 1000
  %32 = select i1 %30, i32 %31, i32 %27
  %33 = getelementptr inbounds [750 x i32], [750 x i32]* @grnbp, i64 0, i64 %8
  %34 = load i32, i32* %33, align 4, !tbaa !2
  %35 = icmp eq i32 %34, 13594
  br i1 %35, label %93, label %36

; <label>:36:                                     ; preds = %18
  br label %37

; <label>:37:                                     ; preds = %50, %36
  %38 = phi i32 [ %53, %50 ], [ %34, %36 ]
  %39 = phi i32 [ %51, %50 ], [ %32, %36 ]
  %40 = sext i32 %38 to i64
  %41 = getelementptr inbounds [13595 x i32], [13595 x i32]* @list, i64 0, i64 %40
  %42 = load i32, i32* %41, align 4, !tbaa !2
  %43 = icmp eq i32 %42, %0
  br i1 %43, label %50, label %44

; <label>:44:                                     ; preds = %37
  %45 = sext i32 %42 to i64
  %46 = getelementptr inbounds [750 x i32], [750 x i32]* @grlibs, i64 0, i64 %45
  %47 = load i32, i32* %46, align 4, !tbaa !2
  %48 = mul nsw i32 %47, 100
  %49 = add nsw i32 %48, %39
  br label %50

; <label>:50:                                     ; preds = %44, %37
  %51 = phi i32 [ %49, %44 ], [ %39, %37 ]
  %52 = getelementptr inbounds [13595 x i32], [13595 x i32]* @links, i64 0, i64 %40
  %53 = load i32, i32* %52, align 4, !tbaa !2
  %54 = icmp eq i32 %53, 13594
  br i1 %54, label %92, label %37

; <label>:55:                                     ; preds = %13, %7
  %56 = phi i32 [ %17, %13 ], [ %12, %7 ]
  %57 = icmp slt i32 %10, %4
  br i1 %57, label %58, label %93

; <label>:58:                                     ; preds = %55
  %59 = getelementptr inbounds [750 x i32], [750 x i32]* @grnbp, i64 0, i64 %8
  %60 = load i32, i32* %59, align 4, !tbaa !2
  %61 = icmp eq i32 %60, 13594
  br i1 %61, label %93, label %62

; <label>:62:                                     ; preds = %58
  %63 = add nsw i32 %56, 100
  br label %64

; <label>:64:                                     ; preds = %86, %62
  %65 = phi i32 [ %60, %62 ], [ %89, %86 ]
  %66 = phi i32 [ %2, %62 ], [ %87, %86 ]
  %67 = sext i32 %65 to i64
  %68 = getelementptr inbounds [13595 x i32], [13595 x i32]* @list, i64 0, i64 %67
  %69 = load i32, i32* %68, align 4, !tbaa !2
  %70 = icmp eq i32 %69, %0
  br i1 %70, label %86, label %71

; <label>:71:                                     ; preds = %64
  %72 = sext i32 %69 to i64
  %73 = getelementptr inbounds [750 x i32], [750 x i32]* @grlibs, i64 0, i64 %72
  %74 = load i32, i32* %73, align 4, !tbaa !2
  %75 = icmp eq i32 %74, 1
  br i1 %75, label %76, label %86

; <label>:76:                                     ; preds = %71
  %77 = sext i32 %66 to i64
  %78 = getelementptr inbounds [80 x i32], [80 x i32]* @mvv, i64 0, i64 %77
  store i32 %63, i32* %78, align 4, !tbaa !2
  %79 = getelementptr inbounds [750 x i32], [750 x i32]* @grlbp, i64 0, i64 %72
  %80 = load i32, i32* %79, align 4, !tbaa !2
  %81 = sext i32 %80 to i64
  %82 = getelementptr inbounds [13595 x i32], [13595 x i32]* @list, i64 0, i64 %81
  %83 = load i32, i32* %82, align 4, !tbaa !2
  %84 = getelementptr inbounds [80 x i32], [80 x i32]* @mvl, i64 0, i64 %77
  store i32 %83, i32* %84, align 4, !tbaa !2
  %85 = add nsw i32 %66, 1
  br label %86

; <label>:86:                                     ; preds = %76, %71, %64
  %87 = phi i32 [ %85, %76 ], [ %66, %71 ], [ %66, %64 ]
  %88 = getelementptr inbounds [13595 x i32], [13595 x i32]* @links, i64 0, i64 %67
  %89 = load i32, i32* %88, align 4, !tbaa !2
  %90 = icmp eq i32 %89, 13594
  br i1 %90, label %91, label %64

; <label>:91:                                     ; preds = %86
  br label %93

; <label>:92:                                     ; preds = %50
  br label %93

; <label>:93:                                     ; preds = %92, %91, %58, %55, %18
  %94 = phi i32 [ %2, %55 ], [ %2, %58 ], [ %2, %18 ], [ %87, %91 ], [ %2, %92 ]
  %95 = phi i32 [ %56, %55 ], [ %56, %58 ], [ %32, %18 ], [ %56, %91 ], [ %51, %92 ]
  %96 = getelementptr inbounds [750 x i32], [750 x i32]* @grlbp, i64 0, i64 %8
  %97 = load i32, i32* %96, align 4, !tbaa !2
  %98 = icmp eq i32 %97, 13594
  br i1 %98, label %306, label %99

; <label>:99:                                     ; preds = %93
  %100 = getelementptr inbounds [750 x i32], [750 x i32]* @grcolor, i64 0, i64 %8
  %101 = load i32, i32* %100, align 4, !tbaa !2
  %102 = sext i32 %101 to i64
  %103 = sext i32 %0 to i64
  %104 = getelementptr inbounds [750 x i32], [750 x i32]* @grlibs, i64 0, i64 %103
  %105 = getelementptr inbounds [750 x i32], [750 x i32]* @grcolor, i64 0, i64 %103
  %106 = load i32, i32* @kosquare, align 4
  %107 = sext i32 %6 to i64
  %108 = icmp sgt i32 %10, 1
  %109 = icmp sge i32 %10, %4
  %110 = icmp sgt i32 %10, 1
  br label %111

; <label>:111:                                    ; preds = %278, %99
  %112 = phi i32 [ %97, %99 ], [ %284, %278 ]
  %113 = phi i32 [ 841, %99 ], [ %279, %278 ]
  %114 = phi i32 [ undef, %99 ], [ %282, %278 ]
  %115 = phi i32 [ 0, %99 ], [ %281, %278 ]
  %116 = phi i32 [ %94, %99 ], [ %280, %278 ]
  %117 = sext i32 %112 to i64
  %118 = getelementptr inbounds [13595 x i32], [13595 x i32]* @list, i64 0, i64 %117
  %119 = load i32, i32* %118, align 4, !tbaa !2
  %120 = sext i32 %119 to i64
  %121 = getelementptr inbounds [841 x i32], [841 x i32]* @lnbn, i64 0, i64 %120
  %122 = load i32, i32* %121, align 4, !tbaa !2
  %123 = mul nsw i32 %122, 30
  %124 = add nsw i32 %123, %95
  %125 = getelementptr inbounds [842 x i32], [842 x i32]* @edge, i64 0, i64 %120
  %126 = load i32, i32* %125, align 4, !tbaa !2
  %127 = add nsw i32 %124, %126
  %128 = getelementptr inbounds [841 x [2 x i32]], [841 x [2 x i32]]* @lnbf, i64 0, i64 %120, i64 %102
  %129 = load i32, i32* %128, align 4, !tbaa !2
  %130 = icmp sgt i32 %129, 1
  br i1 %130, label %131, label %165

; <label>:131:                                    ; preds = %111
  %132 = getelementptr inbounds [841 x i32], [841 x i32]* @fdir, i64 0, i64 %120
  %133 = load i32, i32* %132, align 4, !tbaa !2
  %134 = sext i32 %133 to i64
  %135 = getelementptr inbounds [52 x i32], [52 x i32]* @ldir, i64 0, i64 %134
  %136 = load i32, i32* %135, align 4, !tbaa !2
  %137 = icmp slt i32 %133, %136
  br i1 %137, label %138, label %165

; <label>:138:                                    ; preds = %131
  %139 = sext i32 %136 to i64
  br label %140

; <label>:140:                                    ; preds = %160, %138
  %141 = phi i64 [ %162, %160 ], [ %134, %138 ]
  %142 = phi i32 [ %161, %160 ], [ %127, %138 ]
  %143 = getelementptr inbounds [0 x i32], [0 x i32]* @nbr, i64 0, i64 %141
  %144 = load i32, i32* %143, align 4, !tbaa !2
  %145 = add nsw i32 %144, %119
  %146 = sext i32 %145 to i64
  %147 = getelementptr inbounds [842 x i32], [842 x i32]* @board, i64 0, i64 %146
  %148 = load i32, i32* %147, align 4, !tbaa !2
  %149 = icmp eq i32 %148, %1
  br i1 %149, label %160, label %150

; <label>:150:                                    ; preds = %140
  %151 = sext i32 %148 to i64
  %152 = getelementptr inbounds [750 x i32], [750 x i32]* @grcolor, i64 0, i64 %151
  %153 = load i32, i32* %152, align 4, !tbaa !2
  %154 = icmp eq i32 %153, %101
  br i1 %154, label %155, label %160

; <label>:155:                                    ; preds = %150
  %156 = getelementptr inbounds [750 x i32], [750 x i32]* @grlibs, i64 0, i64 %151
  %157 = load i32, i32* %156, align 4, !tbaa !2
  %158 = mul nsw i32 %157, 30
  %159 = add nsw i32 %158, %142
  br label %160

; <label>:160:                                    ; preds = %155, %150, %140
  %161 = phi i32 [ %159, %155 ], [ %142, %150 ], [ %142, %140 ]
  %162 = add nsw i64 %141, 1
  %163 = icmp eq i64 %162, %139
  br i1 %163, label %164, label %140

; <label>:164:                                    ; preds = %160
  br label %165

; <label>:165:                                    ; preds = %164, %131, %111
  %166 = phi i32 [ %127, %111 ], [ %127, %131 ], [ %161, %164 ]
  switch i32 %122, label %183 [
    i32 1, label %167
    i32 0, label %178
  ]

; <label>:167:                                    ; preds = %165
  %168 = getelementptr inbounds [841 x [2 x i32]], [841 x [2 x i32]]* @lnbf, i64 0, i64 %120, i64 %107
  %169 = load i32, i32* %168, align 4, !tbaa !2
  %170 = icmp ne i32 %169, 0
  %171 = or i1 %170, %109
  br i1 %171, label %185, label %172

; <label>:172:                                    ; preds = %167
  %173 = getelementptr inbounds [841 x i32], [841 x i32]* @nblbp, i64 0, i64 %120
  %174 = load i32, i32* %173, align 4, !tbaa !2
  %175 = sext i32 %174 to i64
  %176 = getelementptr inbounds [13595 x i32], [13595 x i32]* @list, i64 0, i64 %175
  %177 = load i32, i32* %176, align 4, !tbaa !2
  br label %185

; <label>:178:                                    ; preds = %165
  br i1 %110, label %179, label %258

; <label>:179:                                    ; preds = %178
  %180 = getelementptr inbounds [841 x [2 x i32]], [841 x [2 x i32]]* @lnbf, i64 0, i64 %120, i64 %107
  %181 = load i32, i32* %180, align 4, !tbaa !2
  %182 = icmp eq i32 %181, 0
  br i1 %182, label %278, label %187

; <label>:183:                                    ; preds = %165
  %184 = icmp slt i32 %122, 2
  br i1 %184, label %185, label %258

; <label>:185:                                    ; preds = %183, %172, %167
  %186 = phi i32 [ %113, %183 ], [ %177, %172 ], [ %113, %167 ]
  br i1 %108, label %187, label %258

; <label>:187:                                    ; preds = %185, %179
  %188 = phi i32 [ %186, %185 ], [ %113, %179 ]
  %189 = load i32, i32* %104, align 4, !tbaa !2
  %190 = icmp sgt i32 %189, %10
  br i1 %190, label %258, label %191

; <label>:191:                                    ; preds = %187
  %192 = getelementptr inbounds [841 x i32], [841 x i32]* @fdir, i64 0, i64 %120
  %193 = load i32, i32* %192, align 4, !tbaa !2
  %194 = sext i32 %193 to i64
  %195 = getelementptr inbounds [52 x i32], [52 x i32]* @ldir, i64 0, i64 %194
  %196 = load i32, i32* %195, align 4, !tbaa !2
  %197 = icmp slt i32 %193, %196
  br i1 %197, label %198, label %256

; <label>:198:                                    ; preds = %191
  %199 = sext i32 %196 to i64
  %200 = icmp ne i32 %122, 0
  %201 = icmp eq i32 %106, %119
  br label %202

; <label>:202:                                    ; preds = %248, %198
  %203 = phi i32 [ %122, %198 ], [ %249, %248 ]
  %204 = phi i64 [ %194, %198 ], [ %252, %248 ]
  %205 = phi i32 [ %166, %198 ], [ %251, %248 ]
  %206 = phi i32 [ 1, %198 ], [ %250, %248 ]
  %207 = getelementptr inbounds [0 x i32], [0 x i32]* @nbr, i64 0, i64 %204
  %208 = load i32, i32* %207, align 4, !tbaa !2
  %209 = add nsw i32 %208, %119
  %210 = sext i32 %209 to i64
  %211 = getelementptr inbounds [842 x i32], [842 x i32]* @board, i64 0, i64 %210
  %212 = load i32, i32* %211, align 4, !tbaa !2
  %213 = icmp eq i32 %212, %1
  br i1 %213, label %248, label %214

; <label>:214:                                    ; preds = %202
  %215 = icmp ne i32 %212, %0
  %216 = or i1 %215, %200
  br i1 %216, label %217, label %277

; <label>:217:                                    ; preds = %214
  %218 = sext i32 %212 to i64
  %219 = getelementptr inbounds [750 x i32], [750 x i32]* @grcolor, i64 0, i64 %218
  %220 = load i32, i32* %219, align 4, !tbaa !2
  %221 = load i32, i32* %105, align 4, !tbaa !2
  %222 = icmp eq i32 %220, %221
  br i1 %222, label %223, label %233

; <label>:223:                                    ; preds = %217
  %224 = getelementptr inbounds [750 x i32], [750 x i32]* @grlibs, i64 0, i64 %218
  %225 = load i32, i32* %224, align 4, !tbaa !2
  %226 = icmp sgt i32 %225, 2
  br i1 %226, label %229, label %227

; <label>:227:                                    ; preds = %223
  %228 = icmp eq i32 %203, 1
  br i1 %228, label %229, label %233

; <label>:229:                                    ; preds = %227, %223
  %230 = phi i32 [ 1, %227 ], [ %203, %223 ]
  %231 = mul nsw i32 %225, 5
  %232 = add nsw i32 %231, %205
  br label %233

; <label>:233:                                    ; preds = %229, %227, %217
  %234 = phi i32 [ %230, %229 ], [ %203, %227 ], [ %203, %217 ]
  %235 = phi i32 [ 0, %229 ], [ %206, %227 ], [ %206, %217 ]
  %236 = phi i32 [ %232, %229 ], [ %205, %227 ], [ %205, %217 ]
  %237 = sub nsw i32 1, %221
  %238 = icmp eq i32 %220, %237
  br i1 %238, label %239, label %248

; <label>:239:                                    ; preds = %233
  %240 = getelementptr inbounds [750 x i32], [750 x i32]* @grlibs, i64 0, i64 %218
  %241 = load i32, i32* %240, align 4, !tbaa !2
  %242 = icmp slt i32 %241, 3
  br i1 %242, label %243, label %248

; <label>:243:                                    ; preds = %239
  %244 = icmp eq i32 %241, 1
  %245 = select i1 %201, i32 %235, i32 0
  %246 = select i1 %244, i32 %245, i32 %235
  %247 = add nsw i32 %236, 50
  br label %248

; <label>:248:                                    ; preds = %243, %239, %233, %202
  %249 = phi i32 [ %203, %202 ], [ %234, %243 ], [ %234, %239 ], [ %234, %233 ]
  %250 = phi i32 [ %206, %202 ], [ %246, %243 ], [ %235, %239 ], [ %235, %233 ]
  %251 = phi i32 [ %205, %202 ], [ %247, %243 ], [ %236, %239 ], [ %236, %233 ]
  %252 = add nsw i64 %204, 1
  %253 = icmp slt i64 %252, %199
  br i1 %253, label %202, label %254

; <label>:254:                                    ; preds = %248
  %255 = icmp eq i32 %250, 0
  br i1 %255, label %258, label %256

; <label>:256:                                    ; preds = %254, %191
  %257 = icmp eq i32 %122, 0
  br i1 %257, label %278, label %270

; <label>:258:                                    ; preds = %254, %187, %185, %183, %178
  %259 = phi i32 [ %188, %254 ], [ %188, %187 ], [ %186, %185 ], [ %113, %183 ], [ %113, %178 ]
  %260 = phi i32 [ %251, %254 ], [ %166, %187 ], [ %166, %185 ], [ %166, %183 ], [ %166, %178 ]
  %261 = load i32, i32* %104, align 4, !tbaa !2
  %262 = icmp eq i32 %261, 2
  %263 = icmp sgt i32 %260, 199
  %264 = and i1 %263, %262
  br i1 %264, label %265, label %270

; <label>:265:                                    ; preds = %258
  %266 = sext i32 %116 to i64
  %267 = getelementptr inbounds [80 x i32], [80 x i32]* @mvv, i64 0, i64 %266
  store i32 %260, i32* %267, align 4, !tbaa !2
  %268 = add nsw i32 %116, 1
  %269 = getelementptr inbounds [80 x i32], [80 x i32]* @mvl, i64 0, i64 %266
  store i32 %119, i32* %269, align 4, !tbaa !2
  br label %270

; <label>:270:                                    ; preds = %265, %258, %256
  %271 = phi i32 [ %259, %265 ], [ %259, %258 ], [ %188, %256 ]
  %272 = phi i32 [ %260, %265 ], [ %260, %258 ], [ 0, %256 ]
  %273 = phi i32 [ %268, %265 ], [ %116, %258 ], [ %116, %256 ]
  %274 = icmp sgt i32 %272, %115
  %275 = select i1 %274, i32 %272, i32 %115
  %276 = select i1 %274, i32 %119, i32 %114
  br label %278

; <label>:277:                                    ; preds = %214
  br label %278

; <label>:278:                                    ; preds = %277, %270, %256, %179
  %279 = phi i32 [ %188, %256 ], [ %113, %179 ], [ %271, %270 ], [ %188, %277 ]
  %280 = phi i32 [ %116, %256 ], [ %116, %179 ], [ %273, %270 ], [ %116, %277 ]
  %281 = phi i32 [ %115, %256 ], [ %115, %179 ], [ %275, %270 ], [ %115, %277 ]
  %282 = phi i32 [ %114, %256 ], [ %114, %179 ], [ %276, %270 ], [ %114, %277 ]
  %283 = getelementptr inbounds [13595 x i32], [13595 x i32]* @links, i64 0, i64 %117
  %284 = load i32, i32* %283, align 4, !tbaa !2
  %285 = icmp eq i32 %284, 13594
  br i1 %285, label %286, label %111

; <label>:286:                                    ; preds = %278
  %287 = icmp sgt i32 %281, 0
  br i1 %287, label %288, label %295

; <label>:288:                                    ; preds = %286
  %289 = sext i32 %0 to i64
  %290 = getelementptr inbounds [750 x i32], [750 x i32]* @grlibs, i64 0, i64 %289
  %291 = load i32, i32* %290, align 4, !tbaa !2
  %292 = icmp ne i32 %291, 2
  %293 = icmp slt i32 %281, 200
  %294 = or i1 %293, %292
  br i1 %294, label %297, label %295

; <label>:295:                                    ; preds = %288, %286
  %296 = icmp eq i32 %279, 841
  br i1 %296, label %306, label %297

; <label>:297:                                    ; preds = %295, %288
  %298 = phi [80 x i32]* [ @mvl, %295 ], [ @mvv, %288 ]
  %299 = phi i32 [ %279, %295 ], [ %281, %288 ]
  %300 = phi [80 x i32]* [ @mvv, %295 ], [ @mvl, %288 ]
  %301 = phi i32 [ %95, %295 ], [ %282, %288 ]
  %302 = sext i32 %280 to i64
  %303 = getelementptr inbounds [80 x i32], [80 x i32]* %298, i64 0, i64 %302
  store i32 %299, i32* %303, align 4, !tbaa !2
  %304 = add nsw i32 %280, 1
  %305 = getelementptr inbounds [80 x i32], [80 x i32]* %300, i64 0, i64 %302
  store i32 %301, i32* %305, align 4, !tbaa !2
  br label %306

; <label>:306:                                    ; preds = %297, %295, %93
  %307 = phi i32 [ %304, %297 ], [ %280, %295 ], [ %94, %93 ]
  ret i32 %307
}

attributes #0 = { norecurse nounwind ssp "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="cyclone" "target-features"="+crypto,+neon,+zcm,+zcz" "unsafe-fp-math"="false" "use-soft-float"="false" }

!llvm.module.flags = !{!0}
!llvm.ident = !{!1}

!0 = !{i32 1, !"PIC Level", i32 2}
!1 = !{!"clang version 5.0.0 (http://llvm.org/git/clang 3915a28878389759607b4562eba548dedf216298) (http://llvm.org/git/llvm 5111533e9ca30d1c6470e0df17ebcacfb685b2d0)"}
!2 = !{!3, !3, i64 0}
!3 = !{!"int", !4, i64 0}
!4 = !{!"omnipotent char", !5, i64 0}
!5 = !{!"Simple C/C++ TBAA"}
