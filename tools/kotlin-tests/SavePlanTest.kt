import com.dlof.rinlang.*
fun main() {
    check(ProjectSavePlan.uniqueName(listOf("a", "b"), "x") == "x")
    check(ProjectSavePlan.uniqueName(listOf("X", "x-2"), "x") == "x-3")
    check(ProjectSavePlan.uniqueName(emptyList(), "مشروع") == "مشروع")
    check(ProjectSavePlan.shouldSkip(".rin_state") && ProjectSavePlan.shouldSkip("src/.import_1/a.rin"))
    check(!ProjectSavePlan.shouldSkip(".gitignore") && !ProjectSavePlan.shouldSkip("src/utils.rin"))
    check(ProjectSavePlan.needsCopy(10, 5000, -1, 0))                 // مفقود
    check(ProjectSavePlan.needsCopy(10, 5000, 11, 9000))              // حجم مختلف
    check(ProjectSavePlan.needsCopy(10, 9000, 10, 5000))              // المصدر أحدث
    check(!ProjectSavePlan.needsCopy(10, 5000, 10, 5000))             // متطابق
    check(!ProjectSavePlan.needsCopy(10, 6500, 10, 5000))             // فرق أقل من ثانيتين
    println("OK")
}
